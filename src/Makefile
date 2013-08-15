
#export DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib
#CC=gcc
CFLAGS=-std=gnu99 -g -O3 -Werror

SRCS=ai.c hash.c util.c journal.c
OBJS=ai.o hash.o util.o journal.o
INCLUDES=ai.h hash.h util.h journal.h
AR=starthinker.a

all: $(AR)

starthinker.a: $(OBJS) $(INCLUDES)
	ar -rcs $(AR) *.o

clean:
	rm -f *.o $(AR)
	

