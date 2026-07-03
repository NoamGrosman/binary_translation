#!/bin/bash
# Rebuild the submission zip: bprofile.so + src/ (sources, makefiles, REDAME.txt).
cd ~/binary_translation/ex4 || exit 1

rm -f src/README.txt              # superseded by src/REDAME.txt (PDF's required name)
cp src/obj-intel64/bprofile.so bprofile.so

rm -f ex4.zip
zip -q ex4.zip bprofile.so \
    src/bprofile.cpp src/bprofile_orig.cpp \
    src/makefile src/makefile.rules src/REDAME.txt
echo "zip exit: $?"
unzip -l ex4.zip
