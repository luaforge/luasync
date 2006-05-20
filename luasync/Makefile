# $Id: Makefile,v 1.5 2006-05-20 00:11:01 ezdy Exp $
# only for macosx now ;)

CFLAGS=-I/usr/local/include -O2 -Winline -Wall

LDFLAGS=-bundle -undefined dynamic_lookup
LD=export MACOSX_DEPLOYMENT_TARGET="10.3"; gcc

all: luasync.so
clean:
	rm -f *.so *.o

event.h:
	ln -s event/select.h event.h
event.o: event.c event.h
buf.o: buf.c buf.h
err.o: err.c err.h
net.o: net.c net.h event.h

luasync.o: luasync.c

luasync.so: buf.o err.o net.o luasync.o event.o
	$(LD) $(LDFLAGS) buf.o err.o net.o event.o luasync.o -o luasync.so

