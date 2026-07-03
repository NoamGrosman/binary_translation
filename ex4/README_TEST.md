# Zen4 / AVX-512 verification of the ex4 tool — instructions

You're running this because the dev machine (Intel Meteor Lake) has **no
AVX-512**, so the real `cpugcc_r_base.Oz-m64` (the only PIE binary in the
course set) could never be executed to completion there. Your 7800X3D can.

## What to run

```bash
git clone https://github.com/NoamGrosman/binary_translation.git   # or git pull
cd binary_translation/ex4
PIN_ROOT=<your pin-external-4.0-99633-g5ca9893f2 kit> bash run_zen4_test.sh
```

That's it. Takes ~10–20 minutes. Results are printed and written to
`zen4_results.txt` — send that file back.

## What it does

For each binary (cpugcc — the main event, sgcc_peak, cc1, sgcc_base):
1. runs it **natively** on your machine to produce a reference `200.s`;
2. runs it under the prebuilt `bprofile.so` (Pin probe mode, `-prof_time 10`)
   several times and **byte-compares** every output against the native one;
3. checks the tool's built-in TC verifier reports `0 bad branch/rip targets`.

cpugcc also gets one run under `bprofile_orig.so` (the unoptimized baseline).
The bzip2 test auto-enables if `input-long.txt` exists (gunzip
`../ex3/input-long.txt.gz` if you want it).

## Notes

- The prebuilt `.so` was compiled against **pin-4.0-99633-5ca9893f2** — the
  same kit version you used, so it should load as-is. If it doesn't, the
  script rebuilds from `src/` automatically (needs `make` + the kit's g++).
- Everything needed is in the repo: `cc1` and `200.i` under `ex4/`, the other
  binaries under `ex3/` (the script finds them there automatically).
- Expected final line: `ZEN4_RESULT: ALL TESTS PASSED`.
