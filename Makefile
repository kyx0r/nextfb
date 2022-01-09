CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

OBJS = fbpad.o term.o pad.o draw.o font.o isdw.o scrsnap.o

all: fbpad
fbpad.o: conf.h
term.o: conf.h
pad.o: conf.h
.c.o:
	$(CC) -c $(CFLAGS) $<
fbpad: $(OBJS)
	cd ./fbpad_mkfn && ./gen.sh
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	rm -f *.o
clean:
	rm -f *.o fbpad
