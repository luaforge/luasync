/*
 * $Id: event.c,v 1.8 2006-05-29 07:19:30 ezdy Exp $
 *
 * this used to be libevent. which was quite overkill and
 * overbloat, so we're stuck with our own event notification
 * again :/
 */

#define EVENT_C


#include "ll.h"


#include "event.h"

#if 0
#define DEBUG(fmt...) { fprintf(stderr, fmt); fprintf(stderr, "\n"); fflush(stderr); }
#else
#define DEBUG(...)
#endif

static	mtime	now;
static	llist	timers;

static	void	updatenow()
{
	struct	timeval tv;
	gettimeofday(&tv, NULL);
	now = (mtime) tv.tv_sec * 1000;
	now += (mtime)tv.tv_usec / 1000;
	DEBUG("NOW: %lld, tv.tv_sec=%d\n", now, tv.tv_sec);
}

/* set new event mask for a socket */
int	event_set(lua_State *L)
{
	struct	sock *sock = tosock(L, 1);
	int	newmask = sock->evmask;

	if (!lua_isnil(L, 2)) {
		if (lua_toboolean(L, 2))
			newmask |= EV_READ;
		else
			newmask &= ~EV_READ;
	}

	if (!lua_isnil(L, 3)) {
		if (lua_toboolean(L, 3))
			newmask |= EV_WRITE;
		else
			newmask &= ~EV_WRITE;
	}

	/* change only if needed */
	if (newmask != sock->evmask) {
		DEBUG("ev_set(%p,%d)", sock, newmask);
		ev_set(sock, newmask);
	}
	sock->evmask = newmask;
	return 0;
}

static	void	timer_schedule(struct timer *t, int timeout)
{
	llist	*ll;
	struct	timer *st;

	if (timeout <= 0)
		return;

	t->expired = 0;
	t->expire = now + timeout;
	DEBUG("timer expires @ %lld, now=%lld\n", t->expire, now);

	/* shortcut -> see if we're the last timer right away */
	if (!ll_empty(&timers)) {
		st = ll_get(timers.prev, struct timer, list);
		if (t->expire >= st->expire) {
			ll_add(&st->list, &t->list);
			return;
		}
	} else {
		ll_add(&timers, &t->list);
		return;
	}

	/* otherwise exhaustive search */
	ll_for(timers, ll) {
		st = ll_get(ll, struct timer, list);
		if (t->expire < st->expire) {
			ll_add(st->list.prev, &t->list);
			return;
		}
	}

	/* never reached */
	abort();
}

/* set up a new or reschedule existing timer */
int	event_timer(lua_State *L)
{
	struct	timer *timer;
	int	timeout = luaL_checkint(L, 2);

	/* arg -> timer struct mapping */
	lua_getfield(L, LUA_REGISTRYINDEX, ID2TIMER);
	lua_pushvalue(L, 1);
	lua_rawget(L, -2);
	if (!(timer = lua_touserdata(L, -1))) {
		/* create new timer */
		lua_pushvalue(L, 1);
		timer = lua_newuserdata(L, sizeof(struct timer));
		/* id => timer mapping */
		lua_rawset(L, -4);

		lua_getfield(L, LUA_REGISTRYINDEX, TIMER2ID);
		/* timer -> id mapping */
		lua_pushlightuserdata(L, timer);
		lua_pushvalue(L, 1);
		lua_rawset(L, -3);

		LL_CLEAR(timer->list);
		timer->expired = 0;
	}

	/* timer not expired yet, remove it from the queue for reschedule */
	if (!timer->expired) {
		ll_del(&timer->list);
		LL_CLEAR(timer->list);
	}

	/* insert the timer into the timer list */
	if (ll_empty(&timer->list))
		timer_schedule(timer, timeout);
	return 0;
}

/* timer userdata gc, means timer id was already gced. cancel the timer if it was in list */
int	event_timer_gc(lua_State *L)
{
	struct	timer *t = lua_touserdata(L, 1);
	if (!ll_empty(&t->list)) {
		ll_del(&t->list);
		LL_CLEAR(t->list);
		lua_getfield(L, LUA_REGISTRYINDEX, TIMER2ID);
	}
	return 0;
}

int	event_poll(lua_State *L)
{
	struct	sock *sock;
	mtime	ttw = 1000;
retry:
	updatenow();
	/* return timers first, if any */
	if (!ll_empty(&timers)) {
		struct timer *t = ll_get(timers.next, struct timer, list);
		if (t->expire <= now) {
			DEBUG("timer %p expire, expire=%lld, now=%lld", t, t->expire, now);
			ll_del(&t->list);
			LL_CLEAR(t->list);
			t->expired = 1;
			lua_getfield(L, LUA_REGISTRYINDEX, TIMER2ID);
			lua_pushlightuserdata(L, t);
			lua_rawget(L, -2);
			if (lua_isnil(L, -1)) {
				lua_settop(L, 0);
				goto retry;
			}
			return 1;
		}
		ttw = t->expire - now;
	}

	if (!(sock = ev_get())) {
		if (ev_wait(ttw) < 0)
			return 0;
		updatenow();
		if (!(sock = ev_get()))
			goto retry;
	}

	DEBUG("received event(s) on sock %p, fd=%d\n", sock, sock->fd);
	lua_getfield(L, LUA_REGISTRYINDEX, TIMER2ID);
	lua_pushlightuserdata(L, sock);
	lua_rawget(L, -2);
	lua_pushboolean(L, ev_res(sock) & EV_READ);
	lua_pushboolean(L, ev_res(sock) & EV_WRITE);
	return 3;
}

static	luaL_reg ev_meth[] = {
	{ "set",	event_set },
	{ "timer",	event_timer },
	{ "poll",	event_poll },
	{ NULL, NULL }
};

int event_init(lua_State *L)
{
	ev_init();
	LL_CLEAR(timers);

	/* id -> timer mapping, where 'id' is weak */
	lua_pushliteral(L, ID2TIMER);
	lua_newtable(L);	/* our weak table */
	lua_newtable(L);	/* our meta table */
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "k"); /* weak keys */
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);

	/* timer -> id mapping, where 'id' is weak */
	lua_pushliteral(L, TIMER2ID);
	lua_newtable(L);	/* our weak table */
	lua_newtable(L);	/* our meta table */
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "v"); /* weak values */
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);

	luaL_register(L, "event", ev_meth);
	return 0;
}

