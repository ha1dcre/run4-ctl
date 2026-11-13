# RUN4 CTL. Everything builds under build/ (kept out of git). Host simulator needs the venv (Pillow, for fm1-base's fonts).
PY ?= .venv/bin/python
B  := build

test: unit sim

unit: $(B)/r4p_test
	$(B)/r4p_test

$(B)/r4p_test: tests/r4p_test.c src/r4p.c src/r4p.h
	@mkdir -p $(B)
	cc -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -o $@ tests/r4p_test.c src/r4p.c

sim:
	sh tests/run_sim.sh

host:
	$(PY) base/tools/build.py app --host

.PHONY: test unit sim host
