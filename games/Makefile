
#export DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib

#CC=gcc
CFLAGS=-std=gnu99 -g -O3 -Werror -ferror-limit=8 -I../src/

TARGETS=tictactoe reversi pig backgammon stratego chess fourup freecell go jeweled rpg
LIBS=../src/starthinker.a

all: $(TARGETS)

clean:
	rm -f $(TARGETS) *.o
	rm -fr *.dSYM

tictactoe: tictactoe.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ tictactoe.c $(LIBS)

fourup: fourup.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ fourup.c $(LIBS)

reversi: reversi.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ reversi.c $(LIBS)

pig: pig.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ pig.c $(LIBS)

backgammon: backgammon.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ backgammon.c $(LIBS)

stratego: stratego.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ stratego.c $(LIBS)
	
chess: chess.c $(LIBS) $(INCLUDES) epd.c
	${CC} ${CFLAGS} -o $@ chess.c $(LIBS)
	
go: go.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ go.c $(LIBS)
	
freecell: freecell.c $(LIBS) $(INCLUDES) cards.c cards.h
	${CC} ${CFLAGS} -DNDEBUG -o $@ freecell.c $(LIBS) cards.c

rpg: rpg.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ rpg.c $(LIBS)
	
jeweled: jeweled.c $(LIBS) $(INCLUDES)
	${CC} ${CFLAGS} -o $@ jeweled.c $(LIBS)
	
%.gcc.s: %.c
	gcc -S $(CFLAGS) $*.c
	mv $*.s $*.gcc.s
	
%.clang.s: %.c
	clang -S $(CFLAGS) $*.c
	mv $*.s $*.clang.s

%.calltree: %
	sample $* > $*.calltree
	filtercalltree $*.calltree -invertCallTree | more

# for testing with Chess EPD files
%.epd.test: %.epd chess
	time ./chess -s -d 16 -i 4 -- $*.epd | tee $*.epd.out

