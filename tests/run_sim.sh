#!/bin/sh
# Host-simulator tests: builds the firmware app natively (base/tools/build.py --host) and runs tests/scenarios/*.txt.
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
  python3 -I tools/fake_run4.py sim "$s" > build/sim/$n.script
  echo "== $n"
  if "$H" build/sim/$n.script build/sim > build/sim/$n.log 2>&1; then rc=0; else rc=$?; fi
  cat build/sim/$n.log
  if [ $rc -ne 0 ] || grep -q '^FAIL' build/sim/$n.log; then echo "** $n FAILED (exit $rc)"; fail=1; fi
done
python3 -I tests/check_btn_order.py build/sim/05_stalled_host.log | tee -a build/sim/05_stalled_host.log || fail=1
for p in build/sim/*.png; do python3 -I tools/pngpack.py "$p" tests/screens/$(basename "$p"); done
mkdir -p tests/audio
for w in build/sim/*.wav; do [ -f "$w" ] && case "$w" in *synth_demo.wav) cp "$w" tests/audio/;; esac; done
ok=$(cat build/sim/*.log | grep -c '^  ok' || true)
bad=$(cat build/sim/*.log | grep -c '^FAIL' || true)
echo "sim: $ok checks ok, $bad failed"
exit $fail
