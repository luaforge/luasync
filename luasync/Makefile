# $Id: Makefile,v 1.4 2006-05-15 07:36:50 ezdy Exp $
# only for macosx now ;)

CFLAGS=-I/usr/local/include -O2 -Winline -Wall

LDFLAGS=-bundle -undefined dynamic_lookup
LD=export MACOSX_DEPLOYMENT_TARGET="10.3"; gcc

all: luasync.so
#buf.so event.so
clean:
	rm -f *.so *.o

event.o: event.c
buf.o: buf.c buf.h
err.o: err.c err.h
net.o: net.c net.h

luasync.o: luasync.c

luasync.so: buf.o err.o net.o luasync.o
	$(LD) $(LDFLAGS) buf.o err.o net.o luasync.o -o luasync.so

