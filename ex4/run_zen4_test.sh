#!/bin/bash
# ============================================================================
# Zen4 (AVX-512) verification of the ex4 bprofile tool.
#
# Purpose: run the final tool on the binaries this machine can execute but
# the dev machine (no AVX-512) could not - above all the REAL cpugcc - and
# byte-compare every output against a native run on THIS machine.
#
# Usage (from the ex4/ directory of the repo):
#   PIN_ROOT=<path-to-pin-4.0-kit> bash run_zen4_test.sh
#
# Results are printed and also written to zen4_results.txt.
# Expected total runtime: ~10-20 minutes.
# ============================================================================
cd "$(dirname "$0")" || exit 1
ulimit -c 0
R=zen4_results.txt
: > $R
say() { echo "$@" | tee -a $R; }

# --- locate pin -------------------------------------------------------------
if [ -n "$PIN_ROOT" ] && [ -x "$PIN_ROOT/pin" ]; then
  PIN="$PIN_ROOT/pin"
elif command -v pin >/dev/null 2>&1; then
  PIN=$(command -v pin)
else
  say "ERROR: set PIN_ROOT to your Pin 4.0 kit (the tool was built with pin-4.0-99633-5ca9893f2; the same kit version loads it as-is)."
  exit 1
fi
say "pin: $PIN ($($PIN -version 2>/dev/null | head -1))"

# --- locate the tool (prebuilt first, rebuild as fallback) -------------------
TOOL=./bprofile.so
TOOL_ORIG=./bprofile_orig.so
probe_tool() { $PIN -t "$1" -prof_time 1 -- /bin/true >/dev/null 2>&1; }
if ! probe_tool $TOOL; then
  say "prebuilt bprofile.so did not load under this Pin kit - rebuilding from src/ ..."
  ( cd src && make PIN_ROOT="${PIN_ROOT:-$(dirname "$PIN")}" obj-intel64/bprofile.so obj-intel64/bprofile_orig.so ) >> $R 2>&1 || { say "BUILD FAILED - see $R"; exit 1; }
  TOOL=src/obj-intel64/bprofile.so
  TOOL_ORIG=src/obj-intel64/bprofile_orig.so
  probe_tool $TOOL || { say "rebuilt tool still fails to load"; exit 1; }
fi
say "tool: $TOOL"
[ -f $TOOL_ORIG ] || TOOL_ORIG=""

# --- CPU capability ----------------------------------------------------------
if grep -q avx512f /proc/cpuinfo; then
  say "CPU: AVX-512 present - full test including cpugcc"
  HAVE_AVX512=1
else
  say "CPU: NO AVX-512 - cpugcc will be skipped (this is not the intended machine)"
  HAVE_AVX512=0
fi

# --- helpers -----------------------------------------------------------------
find_bin() { # echo first existing path among ./$1 ../ex3/$1
  for p in "./$1" "../ex3/$1"; do [ -f "$p" ] && { echo "$p"; return 0; }; done
  return 1
}

overall_fail=0

test_gcc_binary() { # name opt_runs orig_runs
  local name=$1 opt_runs=$2 orig_runs=$3
  local bin; bin=$(find_bin "$name") || { say "--- $name: NOT FOUND, skipped"; return; }
  chmod +x "$bin" 2>/dev/null
  local inp; inp=$(find_bin 200.i) || { say "--- $name: 200.i not found, skipped"; return; }
  say "--- $name ($bin) ---"

  # native reference on THIS machine
  rm -f zt_native.s
  "$bin" "$inp" -o zt_native.s >/dev/null 2>&1
  local nrc=$?
  if [ $nrc -ne 0 ] || [ ! -s zt_native.s ]; then
    say "$name: native run failed (exit $nrc) - cannot test on this machine"
    return
  fi
  say "$name native: OK ($(stat -c%s zt_native.s) bytes)"

  local i rc tool label runs
  for tool_pair in "OPT:$TOOL:$opt_runs" "ORIG:$TOOL_ORIG:$orig_runs"; do
    label=${tool_pair%%:*}; rest=${tool_pair#*:}; tool=${rest%%:*}; runs=${rest##*:}
    [ -z "$tool" ] || [ "$runs" = 0 ] && continue
    for i in $(seq 1 $runs); do
      rm -f zt_out.s
      $PIN -t "$tool" -prof_time 10 -- "$bin" "$inp" -o zt_out.s >/dev/null 2> zt_run.log
      rc=$?
      if [ $rc -eq 0 ] && cmp -s zt_native.s zt_out.s; then
        say "$name $label run$i: PASS  $(grep -o 'TCVERIFY: [0-9]* bad' zt_run.log)"
      else
        say "$name $label run$i: FAIL (exit=$rc)"; tail -4 zt_run.log | tee -a $R
        overall_fail=1
      fi
    done
  done
  grep -m1 "dead-reg optimization" zt_run.log | tee -a $R
}

# --- the tests ---------------------------------------------------------------
if [ $HAVE_AVX512 -eq 1 ]; then
  test_gcc_binary cpugcc_r_base.Oz-m64 3 1     # THE main event
fi
test_gcc_binary sgcc_peak.mytest-m64 2 0       # flags-across-indirect regression test
test_gcc_binary cc1 2 0
test_gcc_binary sgcc_base.mytest-m64 2 0

# bzip2 (only if the input is around)
BZ=$(find_bin bzip2); INP=$(find_bin input-long.txt)
if [ -n "$BZ" ] && [ -n "$INP" ]; then
  chmod +x "$BZ" 2>/dev/null
  say "--- bzip2 ---"
  cp "$INP" zt_input.txt
  "$BZ" -k -f zt_input.txt && mv zt_input.txt.bz2 zt_native.bz2
  $PIN -t "$TOOL" -prof_time 9999 -- "$BZ" -k -f zt_input.txt >/dev/null 2> zt_run.log
  if [ $? -eq 0 ] && cmp -s zt_native.bz2 zt_input.txt.bz2; then
    say "bzip2 OPT: PASS  $(grep -o 'TCVERIFY: [0-9]* bad' zt_run.log)"
  else
    say "bzip2 OPT: FAIL"; overall_fail=1
  fi
else
  say "--- bzip2: input-long.txt not found, skipped (gunzip ../ex3/input-long.txt.gz to enable)"
fi

say ""
if [ $overall_fail -eq 0 ]; then
  say "ZEN4_RESULT: ALL TESTS PASSED"
else
  say "ZEN4_RESULT: FAILURES - send zen4_results.txt back"
fi
