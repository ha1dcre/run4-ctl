#!/bin/sh
# Host-simulator tests: builds the firmware app natively (base/tools/build.py --host) and runs tests/scenarios/*.txt;
# then the app-stems build (six USB audio channels) and tests/scenarios_stems/*.txt; then the app-noamb build (no AMB
# mode) and tests/scenarios_noamb/*.txt, and tests/scenarios_cross/*.txt on the full build again.
# Each scenario is expanded by tools/fake_run4.py (@NAME@ -> sysex HEX), run by the simulator ("expect" lines fail the run),
# and its screenshots are copied to tests/screens/.
set -e
cd "$(dirname "$0")/.."
PY="${PY:-.venv/bin/python}"
mkdir -p build/sim
export PYTHONPATH=tools/hostshim
"$PY" base/tools/build.py app --host >build/host_build.log 2>&1 || { cat build/host_build.log; exit 1; }
H=app/build/host/run4ctl_host
mkdir -p build/sim tests/screens
fail=0
for s in tests/scenarios/*.txt; do
  n=$(basename "$s" .txt)
  # a firmware 0.6 backup (version 1) from 27_usb_backup's image, and its upgrade (tools/r4ctl_backup.py)
  if [ "$n" = 30_backup_v1 ]; then
    python3 -I tools/r4ctl_backup.py downgrade build/sim/r4ctl.bak build/sim/old_v1.bak >/dev/null &&
      python3 -I tools/r4ctl_backup.py upgrade build/sim/old_v1.bak build/sim/old_v1_up.bak >/dev/null || fail=1
  fi
  python3 -I tools/fake_run4.py sim "$s" > build/sim/$n.script
  echo "== $n"
  if "$H" build/sim/$n.script build/sim > build/sim/$n.log 2>&1; then rc=0; else rc=$?; fi
  cat build/sim/$n.log
  if [ $rc -ne 0 ] || grep -q '^FAIL' build/sim/$n.log; then echo "** $n FAILED (exit $rc)"; fail=1; fi
done
"$PY" base/tools/build.py app-stems --host >build/host_stems.log 2>&1 || { cat build/host_stems.log; exit 1; }
for s in tests/scenarios_stems/*.txt; do
  n=stems_$(basename "$s" .txt)
  python3 -I tools/fake_run4.py sim "$s" > build/sim/$n.script
  echo "== $n (app-stems)"
  if app-stems/build/host/run4ctl-stems_host build/sim/$n.script build/sim > build/sim/$n.log 2>&1; then rc=0; else rc=$?; fi
  cat build/sim/$n.log
  if [ $rc -ne 0 ] || grep -q '^FAIL' build/sim/$n.log; then echo "** $n FAILED (exit $rc)"; fail=1; fi
done
# the package without AMB mode (app-noamb: R4_AMB 0), then the full package again with what it backed up
"$PY" base/tools/build.py app-noamb --host >build/host_noamb.log 2>&1 || { cat build/host_noamb.log; exit 1; }
for s in tests/scenarios_noamb/*.txt tests/scenarios_cross/*.txt; do
  case "$s" in *noamb/*) n=noamb_$(basename "$s" .txt); B=app-noamb/build/host/run4ctl-noamb_host;;
                *) n=cross_$(basename "$s" .txt); B=$H;; esac
  python3 -I tools/fake_run4.py sim "$s" > build/sim/$n.script
  echo "== $n"
  if "$B" build/sim/$n.script build/sim > build/sim/$n.log 2>&1; then rc=0; else rc=$?; fi
  cat build/sim/$n.log
  if [ $rc -ne 0 ] || grep -q '^FAIL' build/sim/$n.log; then echo "** $n FAILED (exit $rc)"; fail=1; fi
done
python3 -I tests/check_btn_order.py build/sim/05_stalled_host.log | tee -a build/sim/05_stalled_host.log || fail=1
# protocol 1.4: the descriptions the scenarios logged, decoded and checked by the independent host side (1.5: their LVIEWs)
for d in "lm_amb amb 31_modes" "lm_synth synth 31_modes" "lm_noamb_synth synth noamb_02_noamb_modes" \
         "lm_views_amb amb 32_views" "lm_views_synth synth 32_views"; do
  set -- $d
  if python3 -I tools/fake_run4.py desc build/sim/$1.bin $2 > build/sim/$1.desc 2>&1; then
    tail -1 build/sim/$1.desc | sed 's/^/  ok /' | tee -a build/sim/$3.log
  else
    cat build/sim/$1.desc; echo "FAIL desc $1" | tee -a build/sim/$3.log; fail=1
  fi
done
for p in build/sim/*.png; do python3 -I tools/pngpack.py "$p" tests/screens/$(basename "$p"); done
mkdir -p tests/audio
for w in build/sim/*.wav; do [ -f "$w" ] && case "$w" in *synth_demo.wav) cp "$w" tests/audio/;; esac; done
ok=$(cat build/sim/*.log | grep -c '^  ok' || true)
bad=$(cat build/sim/*.log | grep -c '^FAIL' || true)
echo "sim: $ok checks ok, $bad failed"
exit $fail
