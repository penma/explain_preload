all:
	gcc -fPIC -ggdb -Os -c -Wall -Wextra expl.c
	gcc -shared -Wl,-soname,expl.so -o expl.so    expl.o    -lc -ldl -lexplain

