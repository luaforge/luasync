# $Id: Makefile,v 1.7 2006-06-04 22:23:59 ezdy Exp $
# only for macosx now ;)

CFLAGS=-I/usr/local/include -O0 -ggdb -Winline -Wall

LDFLAGS=-bundle -undefined dynamic_lookup
LD=export MACOSX_DEPLOYMENT_TARGET="10.3"; gcc

all: luasync.so
clean:
	rm -f *.so *.o event.h

event.h:
	ln -s event/select.h event.h
io.o: io.c buf.h err.h
event.o: event.c event.h buf.h
buf.o: buf.c buf.h
err.o: err.c err.h buf.h
net.o: net.c net.h event.h buf.h

luasync.o: luasync.c

luasync.so: buf.o err.o net.o luasync.o event.o io.o
	$(LD) $(LDFLAGS) buf.o err.o net.o event.o luasync.o io.o -o luasync.so

