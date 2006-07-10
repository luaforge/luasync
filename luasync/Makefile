# $Id: Makefile,v 1.13 2006-07-10 00:58:39 ezdy Exp $
# only for macosx now ;)


CFLAGS=-I/usr/local/include -O0 -ggdb -Winline -Wall
#CFLAGS=-I/usr/local/include -DNDEBUG -O3 -Winline -Wall


LDFLAGS=-bundle -undefined dynamic_lookup -L/usr/local/lib -lsqlite3
LD=export MACOSX_DEPLOYMENT_TARGET="10.3"; gcc

all: luasync.so
clean:
	rm -f *.so *.o event.h

event.h:
	ln -s event/select.h event.h
misc.o: misc.c buf.h err.h
io.o: io.c buf.h err.h
event.o: event.c event.h buf.h
buf.o: buf.c buf.h
err.o: err.c err.h buf.h
net.o: net.c net.h event.h buf.h
sql.o: buf.h sql.c
sha1.o: sha1.h sha1.c
luasync.o: luasync.c

luasync.so: buf.o err.o net.o luasync.o event.o io.o misc.o sql.o sha1.o
	$(LD) $(LDFLAGS) buf.o err.o net.o event.o luasync.o io.o misc.o sql.o sha1.o -o luasync.so
metatest.so: metatest.c
	$(CC) $(CFLAGS) metatest.c -c -o metatest.o
	$(LD) $(LDFLAGS) metatest.o -o metatest.so

