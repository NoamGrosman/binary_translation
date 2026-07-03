#!/bin/bash
set -e
cd ~/binary_translation/ex4
cp -f src/obj-intel64/bprofile.so .
rm -f ex4.zip
zip -r ex4.zip bprofile.so src/bprofile.cpp src/bprofile_orig.cpp src/makefile src/makefile.rules src/README.txt
unzip -l ex4.zip
ls -la ex4.zip bprofile.so
