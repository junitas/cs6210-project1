CC=gcc
CFLAGS=-I. -lvirt -Wno-implicit-function-declaration
OBJ = memory_coordinator.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

memory_coordinator: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)