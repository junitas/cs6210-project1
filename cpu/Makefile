CC=gcc
CFLAGS=-I. -lvirt -Wno-implicit-function-declaration
OBJ = vcpu_scheduler.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

vcpu_scheduler: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)