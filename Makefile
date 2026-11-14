# RUN4 CTL. Everything builds under build/ (kept out of git). Host simulator needs the venv (Pillow, for fm1-base's fonts).
PY ?= .venv/bin/python
B  := build

test: unit synth amb sim tools

unit: $(B)/r4p_test
	$(B)/r4p_test

synth: $(B)/synth_test
	$(B)/synth_test

$(B)/synth_test: tests/synth_test.c tests/reverb_ab.c tests/reverb_ref.c src/synth.c src/synth_ui.c src/synth/fm6_core.c src/plate.c src/plate.h
	@mkdir -p $(B)
	cc -std=gnu11 -O2 -g -ffp-contract=off -DR4_NATIVE -Ibase/include -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o $@ tests/synth_test.c -lm

amb: $(B)/amb_test
	$(B)/amb_test

$(B)/amb_test: tests/amb_test.c src/amb/amb.c src/amb/amb.h src/plate.c src/plate.h src/arena.h
	@mkdir -p $(B)
	cc -std=gnu11 -O2 -g -ffp-contract=off -DR4_NATIVE -Ibase/include -Isrc -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o $@ tests/amb_test.c

$(B)/r4p_test: tests/r4p_test.c src/r4p.c src/r4p.h
	@mkdir -p $(B)
	cc -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -o $@ tests/r4p_test.c src/r4p.c

sim:
	sh tests/run_sim.sh

audio: $(B)/synth_test
	@mkdir -p $(B)/audio
	cd $(B)/audio && ../synth_test . >/dev/null && cp ir_old_hall.wav ir_plate_hall.wav reverb_old_hall.wav reverb_plate_hall.wav ../../tests/audio/

tools:
	python3 tools/fake_fm1.py --selftest
	python3 tools/r4ctl_backup.py --selftest

host:
	$(PY) base/tools/build.py app --host

.PHONY: test unit synth amb sim tools host audio
