LIBEXPLAIN_SOURCES = /tmp/libexplain-1.4.D001

all: expl.o auto_funcs.o
	gcc -shared -Wl,-soname,expl.so -o expl.so    expl.o auto_funcs.o   -lc -ldl -lexplain

expl.o: expl.c
	gcc -fPIC -ggdb -Os -c -Wall -Wextra expl.c

auto_funcs.o: auto_funcs.c
	gcc -fPIC -ggdb -Os -c -Wall -Wextra -Wfatal-errors auto_funcs.c

update_autofuncs:
	@test -d $(LIBEXPLAIN_SOURCES) || { printf 'ERROR: libexplain sourcetree not found.\nUse `make update_autofuncs LIBEXPLAIN_SOURCES=/path/to/libexplain-sourcetree`\n' >&2; false; }
	@test -d $(LIBEXPLAIN_SOURCES)/catalogue || { printf 'ERROR: Subdirectory `catalogue` not found in libexplain sourcetree.\nUse `make update_autofuncs LIBEXPLAIN_SOURCES=/path/to/libexplain-sourcetree`\n' >&2; false; }
	./autowrap.pl  $(LIBEXPLAIN_SOURCES)/catalogue/* > auto_funcs.c
