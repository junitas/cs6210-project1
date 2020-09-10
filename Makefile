CC=gcc
CFLAGS=-I. -lvirt -Wno-implicit-function-declaration
DEPS = main.h
OBJ = main.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

main: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)