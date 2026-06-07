Exercise 2 - Edge Profiling Pintool (JIT mode)

Authors:
  Noam Grosman   ID: 318677341
  Or Ederi       ID: 314814849

Files in this submission (src/):
  ex2.cpp          - Pintool source
  makefile         - Pin tool makefile
  makefile.rules   - Pin tool build rules
  README.txt       - Submission Details

The zip contains ex2.so

Build instructions:
  Place the files inside a Pin tool source folder and then:
      <PIN_ROOT>/source/tools/MyTools/

  Build from inside that folder with:
      make PIN_ROOT=<PIN_ROOT> obj-intel64/ex2.so

  The output file will be obj-intel64/ex2.so.

Run instructions:
  After gunzipping the supplied bzip2.gz and input.txt.gz, run:
      <PIN_ROOT>/pin -t obj-intel64/ex2.so -- ./bzip2 -k -f input.txt

  This gives two output files:
      edge-profile.csv   - Part 1: per-BBL edge profile,
                           sorted from hottest -> coldest, with conditional
                           taken/fallthru counts and up to 10 indirect
                           target addresses sorted from hottest -> coldest.
      rtn-output.csv     - Part 2: per-routine register sampling for
                           RAX, RBX, RCX, RDX, RSI, RDI (up to 20
                           successive values per register) with an
                           average delta when at least two samples exist.

Ran with pin 4.0
