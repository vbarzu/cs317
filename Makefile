main: main.c
	gcc -o server main.c -lrt `pkg-config --libs --cflags opencv`