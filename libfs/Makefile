# Target library
lib := libfs.a

CC = gcc

CFLAGS	:= -Wall -Wextra -Werror -g -c -o

all: $(lib)

## TODO: Phase 1
$(lib): disk.o fs.o
	ar rcs $@ $^

disk.o: disk.c disk.h
	$(CC) $(CFLAGS) $@ $<

fs.o: fs.c fs.h
	$(CC) $(CFLAGS) $@ $<

clean:
	rm -rf *.o *.a
	cd ../apps; rm -rf *.o *.d
