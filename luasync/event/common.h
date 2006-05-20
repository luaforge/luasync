
#include <stdint.h>

#define	mtime uint64_t
struct	timer {
	mtime	expire;
	int	expired;
	llist	list;
};

static	struct timeval *t2tv(mtime timeout)
{
	static struct timeval tv;
	tv.tv_sec = timeout/1000;
	tv.tv_usec = (timeout%1000)*1000;
	return &tv;
}


