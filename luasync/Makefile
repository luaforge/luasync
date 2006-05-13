CFLAGS=-I/usr/local/include -O2 -Winline -Wall

LDFLAGS=-bundle -undefined dynamic_lookup
LD=export MACOSX_DEPLOYMENT_TARGET="10.3"; gcc

all: buf.so event.so
clean:
	rm -f *.so *.o

event.o: event.c


buf.o: buf.c buf.h


buf.so: buf.o
	$(LD) $(LDFLAGS) buf.o -o buf.so
event.so: event.o
	$(LD) $(LDFLAGS) event.o -levent -o event.so
