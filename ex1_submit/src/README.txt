Name: Noam Grosman
ID: 318677341
Compilation:
	make PIN_ROOT=$PIN_ROOT obj-intel64/ex1.so
Run:
	$PIN_ROOT/pin -t obj-intel64/ex1.so -- ./bzip2 -k -f input.txt
Output:
	Produces rtn-output.csv in the directory.
