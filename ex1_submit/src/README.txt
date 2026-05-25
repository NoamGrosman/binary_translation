Name: Noam Grosman
ID: 318677341

Helper before running:
	Set PIN_ROOT to the Pin kit root directory before building:
		export PIN_ROOT=/path/to/pin-external-4.0-99633-g5ca9893f2-gcc-linux

Compilation:
	make PIN_ROOT=$PIN_ROOT obj-intel64/ex1.so

Run:
	$PIN_ROOT/pin -t obj-intel64/ex1.so -- ./bzip2 -k -f input.txt

Output:
	Produce rtn-output.csv in the current directory.
