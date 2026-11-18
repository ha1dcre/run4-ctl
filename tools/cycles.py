#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The render's cost per 256-frame block, in instructions, measured in an emulator.

The FM-1's core (JieLi pi32v2, 360 MHz) has no emulator here, so tests/cycles/cyc_bench.c is built for a core of the
same shape (Cortex-M4F: in-order, 16 registers, 32x32->64 multiply, 32-bit divide, a single-precision FPU) with
arm-none-eabi-gcc at the device's levels (the synth's unity build -Os, the o2 units -O2) and run in Unicorn; every
executed basic block is decoded with Capstone and counted. The result is instructions (the method of fm1-base's
docs/bt/BT-CLASSIC.md: instructions as a share of the 360 MHz core, a lower bound: no cache or flash waits) and an
M4-like cycle estimate (loads, taken branches, divides charged extra).

  tools/cycles.py [synth|dub|amb] [--quick] [--check] [--json out.json] [--src DIR]
    --check: exit 1 when a Synth mode block is over the limit (80 % of the budget at K0 cycles an instruction) or the
    render's cost model (src/synth.c SYN_CM_*) estimated less than was measured; --src: build another tree's src/
    (e.g. a git archive of a release) with this tree's bench, for a before / after

Needs: arm-none-eabi-gcc, python3 with unicorn and capstone (pip install unicorn capstone pyelftools). The budget:
APP_BLOCK 256 frames at 44.1 kHz = 5805 us = 2,089,800 cycles at 360 MHz; fm1-base calls app_overload at 90 %.
"""
import json
import os
import subprocess
import sys
import tempfile

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_SP)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET = 256 * 360_000_000 // 44100            # cycles per block at 360 MHz: 2,089,795
LD = """MEMORY { FLASH (rx) : ORIGIN = 0x00100000, LENGTH = 4M  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4M }
ENTRY(main)
SECTIONS {
  .text : { *(.text*) *(.rodata*) } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .data : { *(.data*) } > RAM AT > FLASH
  .bss (NOLOAD) : { *(.bss*) *(COMMON) *(.pool*) *(.noinit*) } > RAM
}
"""
STOP = 0x00F00000


SRC = ROOT                                     # --src DIR: another tree's src/ (a release before, for comparison)


def build(mode, out):
    ld = os.path.join(os.path.dirname(out), "cyc.ld")
    with open(ld, "w") as f:
        f.write(LD)
    opt = os.environ.get("CYC_OPT", "-O2")
    cmd = ["arm-none-eabi-gcc", "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16", opt,
           "-g", "-std=gnu11", "-ffp-contract=off", "-fno-builtin", "-ffreestanding", "-ffunction-sections",
           "-fdata-sections", f"-DCYC_{mode}", "-I", os.path.join(ROOT, "base/include"), "-I", os.path.join(SRC, "src"),
           "-w", "-nostdlib", "-T", ld, "-Wl,--gc-sections",
           "-Wl,-u,cyc_block,-u,cyc_scenario,-u,cyc_init,-u,cyc_out,-u,cyc_info", "-o", out,
           os.path.join(SRC, "tests/cycles/cyc_bench.c"), "-lgcc"]
    subprocess.run(cmd, check=True)


class Emu:
    def __init__(self, elf):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
        self.syms = {}
        with open(elf, "rb") as f:
            e = ELFFile(f)
            for s in e.get_section_by_name(".symtab").iter_symbols():
                if s.name:
                    self.syms[s.name] = s["st_value"]
            self.uc.mem_map(0x00100000, 4 << 20)
            self.uc.mem_map(0x20000000, 8 << 20)
            self.uc.mem_map(STOP, 0x1000)
            for seg in e.iter_segments():
                if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                    self.uc.mem_write(seg["p_vaddr"], seg.data())
        # the FPU on (CPACR cp10/cp11, FPEXC.EN)
        self.uc.reg_write(UC_ARM_REG_C1_C0_2, self.uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        self.uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.blk = {}                         # (addr, size) -> (instructions, est. cycles)
        self.count = None
        self.uc.hook_add(UC_HOOK_BLOCK, self._hook)

    def _cost(self, addr, size):
        code = bytes(self.uc.mem_read(addr, size))
        n = cyc = 0
        for ins in self.cs.disasm(code, addr):
            m = ins.mnemonic.split(".")[0]
            n += 1
            c = 1
            if m in ("udiv", "sdiv"):
                c = 7                         # 2..12 on an M4
            elif m in ("vdiv", "vsqrt"):
                c = 14
            elif m.startswith(("ldr", "vldr")):
                c = 2
            elif m.startswith(("ldm", "stm", "push", "pop", "vpush", "vpop", "vldm", "vstm")):
                c = 1 + ins.op_str.count(",") + 1
            elif m.startswith(("b", "cb")) and m not in ("bic", "bfi", "bfc"):
                c = 3 if m in ("b", "bl", "blx", "bx") else 1   # (conditional: counted taken below, per block)
            cyc += c
        return n, cyc

    def _hook(self, uc, addr, size, _):
        if self.count is None:
            return
        k = (addr, size)
        v = self.blk.get(k)
        if v is None:
            v = self.blk[k] = self._cost(addr, size)
        self.count[0] += v[0]
        self.count[1] += v[1] + 1             # (a block entered: the branch that got here, taken, ~+1..2)

    def call(self, name, a=0, b=0, counted=False):
        uc = self.uc
        uc.reg_write(UC_ARM_REG_SP, 0x20700000)
        uc.reg_write(UC_ARM_REG_R0, a)
        uc.reg_write(UC_ARM_REG_R1, b)
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        self.count = [0, 0] if counted else None
        uc.emu_start(self.syms[name] | 1, STOP)
        r, self.count = self.count, None
        return r

    def info(self, i):
        return int.from_bytes(self.uc.mem_read(self.syms["cyc_info"] + 4 * i, 4), "little")


def pct(x):
    return 100.0 * x / BUDGET


K0 = 3.0                                       # cycles an instruction on the FM-1 (src/synth.c SYN_CM_K0: 768 / 256)
LIMIT = 0.8 * BUDGET / K0                      # the headroom rule: a block's instructions x K0 under 80 % of the budget


class Run:
    """a scenario script: ('s', id, arg) a scene call, ('b', n) n blocks counted, ('f', n) n blocks not counted"""

    def __init__(self, emu, label, model=False):
        self.emu, self.label, self.model = emu, label, model
        self.worst = self.worst_c = self.sum = self.n = self.voices = 0
        self.under = 0                         # SYNTH: the worst (measured - the cost model's estimate) of a block

    def play(self, script):
        for st in script:
            if st[0] == "s":
                self.emu.call("cyc_scenario", st[1], st[2])
            elif st[0] == "f":
                for _ in range(st[1]):
                    self.emu.call("cyc_block")
            else:
                for _ in range(st[1]):
                    n, c = self.emu.call("cyc_block", counted=True)
                    self.worst = max(self.worst, n)
                    self.worst_c = max(self.worst_c, c)
                    self.sum += n
                    self.n += 1
                    if self.model:
                        self.voices = max(self.voices, self.emu.info(0))
                        self.under = max(self.under, n - self.emu.info(2))
        return self

    def row(self):
        r = {"scene": self.label, "worst_instr": self.worst, "worst_cyc_est": self.worst_c,
             "mean_instr": self.sum // max(self.n, 1), "worst_pct": round(pct(self.worst), 1),
             "worst_cyc_pct": round(pct(self.worst_c), 1), "device_pct_k0": round(pct(self.worst * K0), 1),
             "blocks": self.n}
        if self.model:
            r["voices"] = self.voices
            r["model_short"] = self.under
        return r


def synth_scenes(quick):
    out = []
    fx = ("s", 3, 0)
    reset = [("s", 7, 0), ("s", 8, 0), ("f", 2)]
    for nv in ([8, 16] if quick else [4, 8, 16]):
        for p in ([0] if quick else range(8)):
            out.append((f"patch {p} x{nv} fx all", reset + [("s", 0, p), fx, ("f", 2), ("s", 2, nv), ("b", 12),
                                                           ("s", 4, 0), ("b", 4)]))
        out.append((f"worst x{nv} fx all", reset + [("s", 1, 0), fx, ("f", 2), ("s", 2, nv), ("b", 12),
                                                   ("s", 4, 0), ("b", 4)]))
        out.append((f"worst x{nv} reverb", reset + [("s", 1, 0), ("s", 9, 3), ("f", 2), ("s", 2, nv), ("b", 12)]))
        out.append((f"worst x{nv} no fx", reset + [("s", 1, 0), ("f", 2), ("s", 2, nv), ("b", 12)]))
    # chords struck over held ones (steals), the worst patch, everything on; then the guard's steps
    out.append(("worst x8 + 8 struck, fx all", reset + [("s", 1, 0), fx, ("s", 2, 8), ("f", 8), ("s", 5, 8), ("b", 8),
                                                         ("s", 5, 8), ("b", 8)]))
    for shed in range(1, 5):
        out.append((f"worst x8 fx all, shed {shed}", reset + [("s", 6, shed), ("s", 1, 0), fx, ("f", 2), ("s", 2, 8),
                                                              ("b", 8), ("s", 6, 0)]))
    return out


def run_mode(mode, quick, tmp):
    elf = os.path.join(tmp, f"cyc_{mode}.elf")
    build(mode, elf)
    emu = Emu(elf)
    emu.call("cyc_init")
    rows = []
    if mode == "SYNTH":
        for label, sc in synth_scenes(quick):
            rows.append(Run(emu, label, model=True).play(sc).row())
        return rows
    secs = 6 if quick else 20
    blocks = secs * 44100 // 256
    for p in (range(2) if quick else range(8)):
        rows.append(Run(emu, f"piece {p}").play([("s", 0, p), ("b", blocks)]).row())
    # everything at once: the layers, the macros (DUB: Dub, Energy, Space; AMB: the layers and the space), DUB's
    # effects grid and PERFORM's echo throw; then the next chord and a freeze
    if mode == "DUB":
        def par(i, v):
            return ("s", 8, (i << 16) | (v + 32768))
        up = [par(i, 100) for i in (0, 1, 2, 3, 4, 7, 8, 10, 30, 33)]   # the layers, density, hits, echo, reverb,
        for cv in (0, 1, 4):                                               # crackle, cloud; Saws, Keys, Bell
            sc = [("s", 0, 0)] + up + [par(22, cv), ("s", 1, (1 << 8) | 200), ("s", 1, (2 << 8) | 200),
                                       ("s", 1, (3 << 8) | 200), ("s", 2, 0xFFFF), ("s", 3, 16), ("b", blocks),
                                       ("s", 6, 0), ("s", 5, 1), ("b", blocks // 2), ("s", 5, 0), ("s", 2, 0),
                                       ("s", 3, 0)]
            rows.append(Run(emu, f"everything up, chord voice {cv}").play(sc).row())
    else:
        sc = [("s", 0, 2)] + [("s", 1, (p << 8) | 100) for p in range(12)] + [("b", blocks), ("s", 6, 0), ("s", 5, 1),
                                                                              ("b", blocks // 2), ("s", 5, 0)]
        rows.append(Run(emu, "everything up").play(sc).row())
    return rows


def main():
    global SRC
    args = sys.argv[1:]
    if "--src" in args:
        SRC = os.path.abspath(args[args.index("--src") + 1])
    quick = "--quick" in args
    check = "--check" in args
    out = args[args.index("--json") + 1] if "--json" in args else None
    modes = [a.upper() for a in args if a.lower() in ("synth", "dub", "amb")] or ["SYNTH", "DUB", "AMB"]
    res = {"budget_cycles": BUDGET, "k0": K0, "limit_instr": int(LIMIT)}
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        for m in modes:
            rows = run_mode(m, quick, tmp)
            res[m] = rows
            print(f"{m}: budget {BUDGET} cycles a block (256 frames, 360 MHz); limit {int(LIMIT)} instructions "
                  f"(80 % at {K0} cycles each)")
            for r in rows:
                extra = f"  voices {r['voices']}  model short by {r['model_short']}" if "voices" in r else ""
                print(f"  {r['scene']:<34} worst {r['worst_instr']:>7} instr ({r['worst_pct']:5.1f} %, "
                      f"{r['device_pct_k0']:5.1f} % at K0)  M4 est {r['worst_cyc_est']:>7}  mean {r['mean_instr']:>7}"
                      + extra)
                if m == "SYNTH" and (r["worst_instr"] > LIMIT or r["model_short"] > 0):
                    bad.append(f"{m} {r['scene']}")
            sys.stdout.flush()
    if out:
        with open(out, "w") as f:
            json.dump(res, f, indent=1)
    if check and bad:
        print("over the limit, or the cost model under the measurement:", ", ".join(bad))
        sys.exit(1)


if __name__ == "__main__":
    main()
