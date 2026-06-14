# Exercise 3

Fix the provided Pintool in Probe mode called **btranslate.cpp** (located at the course site at:
<https://moodle25.technion.ac.il/mod/resource/view.php?id=136832>) so that it manages to
successfully translate the provided binary program **cpugcc_r_base.mytest-m64** and generate the
correct output file **200.s** using the following pin command:

```
$ <pindir>/pin -t obj-intel64/btranslate.so -- cpugcc_r_base.Oz-m64 200.i -o 200.s
```

Currently, the **btranslate.cpp** pintool fails to properly translate the executable and exits with a
"Segmentation fault" error without generating the output file **200.s**.

Use **any** available methods and tools to fix the pintool in order to generate the correct output.

Explain the issue that you found and how you fixed it in an attached **REDAME.txt** file. *(sic — README.txt)*

## Tips

Try to isolate the faulty routine as shown in class by limiting the number of candidate routines in
function **create_tc** of the pintool.

## Submission requirements

The submission of this exercise is **in pairs only**.

Submit 1 compressed file called **"ex3.zip"** into the moodle exercise 3 link containing the following files:

1. The binary of your pintool **ex3.so** (compiled, and tested by you that it runs and gives the result).
2. A directory called **`src`** containing all the sources of your pintool along with the make files
   **`makefile`**, **`makefile.rules`** and a **REDAME.txt** *(sic)* file that includes the following:
   - a. names + id numbers
   - b. How to run the tool.

**Submission deadline is Sunday, June 21, 2026 at midnight.**
