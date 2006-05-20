/*
 * ev_init(), ev_sinit(fd)
 * ev_set(fd,mask)
 * ev_unset(fd,mask)
 * ev_wait(time)
 * ev_get()
 */

typedef struct	event_s {
	llist	list; /* this MUST be first, see the ll_get pigginess below */
	int	mask;
} event_t;

#define	EV_READ	(POLLIN|POLLERR|POLLHUP)
#define	EV_WRITE (POLLOUT|POLLERR|POLLHUP)

/* mapping idx->fd */
static	struct	sock *idxmap = NULL;

/* mapping idx->pollfd */
struct	pollfd *pollmap = NULL;

static	int	palloc = 0;
static	int	curfd = 0;

#define ev_init()
static	inline	int	ev_sinit(struct sock *s)
{
	LL_CLEAR(s->event.list);
}

static	inline	void	ev_set(struct sock *s, int mask)
{
	s->event.mask = mask;

	/* not added yet? */
	if (ll_empty(&s->event.list)) {
		fdcount++;
		ll_add(&slist, &s->event.list);
		need_recompute = 1;
		return;
	}

	/* already added, just set the mask */
	pollmap[s->event.idx].events = mask;
}

static	inline	void ev_unset(struct sock *s)
{
	/* already removed .. */
	if (ll_empty(&s->event.list))
		return;

	fdcount--;
	ll_del(&s->event.list);
	ev_sinit(s);
	idxmap[s->event.idx] = NULL;
	need_recompute = 1;
}

static	inline	int	ev_wait(int timeout)
{
	if (need_recompute) {
		llist	*ll;
		int	idx = 0;

		if (fdcount >= palloc) {
			palloc = fdcount + FD_PREALLOC;
			pollmap = realloc(pollmap, palloc * sizeof(struct pollfd));
			idxmap = realloc(idxmap, palloc * sizeof(struct sock *));
		}

		ll_for(slist, ll) {
			struct	sock *s = ll_get(ll, struct sock, event); /* omG :) */
			idxmap[idx] = s;
			pollmap[idx].fd = s->fd;
			pollmap[idx++].events = s->event.mask;
		}
		assert(idx == fdcount);
		need_recompute = 0;
	}
	nres = poll(pollmap, fdcount, timeout);
	if (nres < 0) {
		nres = 0;
		return -1;
	}
	curfd = 0;
	return 0;
}

static	inline	struct sock *ev_get()
{
	if (nres <= 0)
		return NULL;

	while (curfd < fdcount) {
		if (!idxmap[curfd]) {
			curfd++;
			continue;
		}
		assert(idxmap[curfd]->event.idx == curfd);
		if (pollmap[curfd].revents & idxmap[curfd]->event.mask) {
			nres--;
			return idxmap[curfd++];
		}
		curfd++;
	}
	return NULL;
}

#define ev_res(s) (pollmap[s->idx].revents)

