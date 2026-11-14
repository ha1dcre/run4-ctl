# RUN4 CTL. Everything builds under build/ (kept out of git). Host simulator needs the venv (Pillow, for fm1-base's fonts).
PY ?= .venv/bin/python
B  := build

test: unit synth sim tools

unit: $(B)/r4p_test
	$(B)/r4p_test

synth: $(B)/synth_test
	$(B)/synth_test

$(B)/synth_test: tests/synth_test.c src/synth.c src/synth_ui.c src/synth/fm6_core.c
	@mkdir -p $(B)
	cc -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o $@ tests/synth_test.c -lm

$(B)/r4p_test: tests/r4p_test.c src/r4p.c src/r4p.h
	@mkdir -p $(B)
	cc -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -o $@ tests/r4p_test.c src/r4p.c

sim:
	sh tests/run_sim.sh

tools:
	python3 tools/fake_fm1.py --selftest

host:
	$(PY) base/tools/build.py app --host

.PHONY: test unit synth sim tools host
