/*
 * ev_init()
 * ev_set(fd,mask,arg)
 * ev_unset(fd,mask,arg)
 * ev_wait(time)
 * ev_get()
 * ev_res(fd)
 * ev_data(fd)
 */
#ifndef EVENT_H
#define EVENT_H

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>


#define event_t llist

#include "event/common.h"
#include "net.h"

#define EV_READ		1
#define	EV_WRITE	2

#ifdef EVENT_C
#define EXTERN
#else
#define EXTERN extern
#endif
EXTERN	fd_set	rfds, wfds, trfds, twfds;
EXTERN	int	fdmax, nres;
EXTERN	llist	slist, *curfd;
#define ev_init() { LL_CLEAR(slist); }

#define ev_sinit(sock) LL_CLEAR((sock->event))

static	inline int ev_set(struct sock *s, int mask)
{
	int	n = 0;
	if (mask & EV_READ)
		FD_SET(s->fd, &rfds);
	else
		FD_CLR(s->fd, &rfds);

	if (mask & EV_WRITE)
		FD_SET(s->fd, &wfds);
	else
		FD_CLR(s->fd, &wfds);

	if ((n = ll_empty(&s->event)))
		ll_add(&slist, &s->event);

	if (s->fd > fdmax)
		fdmax = s->fd;
	return n;
}

static	inline void ev_unset(struct sock *s)
{
	FD_CLR(s->fd, &rfds);
	FD_CLR(s->fd, &wfds);

	/* might happen someone will delete next fd in list */
	if (curfd == &s->event)
		curfd = s->event.next;

	ll_del(&s->event);
	ev_sinit(s);

	/* deleted highest socket. find new fdmax */
	if (s->fd == fdmax) {
		llist	*ll;
		fdmax = 0;
		ll_for(slist, ll) {
			struct sock *sf = ll_get(ll, struct sock, event);
			if (sf->fd > fdmax)
				fdmax = sf->fd;
		}
	}
}

static	inline int	ev_wait(int timeout)
{
	memcpy(&trfds, &rfds, sizeof(rfds));
	memcpy(&twfds, &wfds, sizeof(wfds));

	nres = select(fdmax+1, &trfds, &twfds, NULL, t2tv(timeout));
	if (nres < 0) {
		nres = 0;
		return -1;
	}
	curfd = slist.next;
	return 0;
}

static	inline	struct sock *ev_get(void)
{
	if (nres <= 0)
		return NULL;

	while ((curfd != &slist) && (nres > 0)) {
		struct sock *sf = ll_get(curfd, struct sock, event);

		if (FD_ISSET(sf->fd, &rfds) || FD_ISSET(sf->fd, &wfds)) {
			nres--;
			curfd = sf->event.next;
			return sf;
		}
		curfd = sf->event.next;
	}
	return NULL;
}

#define ev_res(s) ((FD_ISSET(s->fd, &rfds)?EV_READ:0)|(FD_ISSET(s->fd, &wfds)?EV_WRITE:0))
#endif
