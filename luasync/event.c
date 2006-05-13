#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#include <event.h>
#include "ll.h"

#define EVHANDLE "ev*"
#define EVENTAB "event.ev2ud"

int	default_timeout = 60000;
llist	completed = LL_INIT(completed);

#define	getevent(L) luaL_checkudata(L, 1, EVHANDLE)

/*************************************************************************
 * structs 
 *************************************************************************/
/* this is the event as seen by lua */
struct	luaevent {
	llist	clist;
	int	started:1;
	int	completed:1;
	int	timeout;
	short	mask;
	struct	event ev;
};

/*************************************************************************
 * utility
 *************************************************************************/
static	struct timeval *t2tv(int timeout)
{
	static struct timeval tv;
	tv.tv_sec = timeout/1000;
	tv.tv_usec = (timeout%1000)*1000;
	return &tv;
}

static	void	event_start(struct luaevent *le)
{
	if (!le->started)
		return;
	/*printf("%d\n", le->timeout);*/
	event_add(&le->ev, le->timeout?t2tv(le->timeout):NULL);
}

static	void	event_stop(struct luaevent *le)
{
	if (!le->started)
		return;
	event_del(&le->ev);
}


static	void	event_cb(int fd, short event, void *d)
{
	struct	luaevent *ev = d;
	if (ev->completed)
		return;
	ev->completed = 1;
	ll_add(completed.prev, &ev->clist);
}

/* parse given mask for +-rw */
static	short	parsemask(short orig, char *mask)
{
	char *p = mask;
	int mode = 1; /* + */
	while (*p) {
		short flag;
		switch (*p++) {
			case '+':
				mode = 1;
				continue;
			case '-':
				mode = 0;
				continue;
			case 'r':
				flag = EV_READ;
				break;
			case 'w':
				flag = EV_WRITE;
				break;
			default:
				continue;
		}
		if (mode)
			orig |= flag;
		else
			orig &= ~flag;
	}
	return orig;
}

static	char *printmask(short mask)
{
	if ((mask & (EV_READ|EV_WRITE)) == (EV_READ|EV_WRITE))
		return "rw";
	if (mask & EV_READ)
		return "r";
	if (mask & EV_WRITE)
		return "w";
	return "";
}


/*************************************************************************
 * lua visible
 *************************************************************************/
static	int	ev_add(lua_State *L)
{
	int	fd;
	int	oneshot = 0;
	struct	luaevent *le;
	char	*mask;
	int	timeout;


	/* get args */
	fd = luaL_checkint(L, 1);
	mask = (char *) luaL_optstring(L, 2, "rw");
	timeout = luaL_optint(L, 3, default_timeout);
	if (lua_isboolean(L, 4))
		oneshot = lua_toboolean(L, 4);

	le = lua_newuserdata(L, sizeof(*le));
	le->mask = parsemask(oneshot?0:EV_PERSIST, mask);
	event_set(&le->ev, fd, le->mask, event_cb, le);
	le->completed = le->started = 0;
	le->timeout = timeout;

	le->started = 1;
	event_start(le);

	/* assign methods */
	luaL_getmetatable(L, EVHANDLE);
	lua_setmetatable(L, -2);

	/* and map to light userdata */
	lua_pushliteral(L, EVENTAB);
	lua_rawget(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, le);
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	return 1;
}

static	int	ev_fd(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (lua_isnumber(L, 2)) {
		int fd = lua_tointeger(L, 2);
		if (le->ev.ev_fd != fd) {
			event_stop(le);
			event_set(&le->ev, fd, le->mask, event_cb, le);
			event_start(le);
		}
	}
	lua_pushnumber(L, le->ev.ev_fd);
	return 1;
}

static	int	ev_mask(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (lua_isstring(L, 2)) {
		short mask = parsemask(le->mask, (char *) lua_tostring(L, 2));
		if (le->mask != mask) {
			le->mask = mask;
			event_stop(le);
			event_set(&le->ev, le->ev.ev_fd, le->mask, event_cb, le);
			event_start(le);
		}
	}
	lua_pushstring(L, printmask(le->mask));
	return 1;
}

static	int	ev_timeout(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (lua_isnumber(L, 2)) {
		int timeout = lua_tointeger(L, 2);
		if (timeout <= 0)
			timeout = 0;
		if (le->timeout != timeout) {
			event_stop(le);
			event_set(&le->ev, le->ev.ev_fd, le->mask, event_cb, le);
			event_start(le);
		}
	}
	lua_pushinteger(L, le->timeout);
	return 1;
}

static	int	ev_oneshot(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (lua_isboolean(L, 2)) {
		short oneshot = lua_toboolean(L, 2)?0:EV_PERSIST;
		if ((le->mask & EV_PERSIST) != oneshot) {
			le->mask |= oneshot;
			event_stop(le);
			event_set(&le->ev, le->ev.ev_fd, le->mask, event_cb, le);
			event_start(le);
		}
	}
	lua_pushboolean(L, !(le->mask & EV_PERSIST));
	return 1;
}



static	int	ev_start(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (!le->started) {
		le->started = 1;
		event_start(le);
	}
	return 0;
}

static	int	ev_stop(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	if (le->started) {
		event_stop(le);
		le->started = 0;
	}
	return 0;
}

static	int	ev_del(lua_State *L)
{
	struct	luaevent *le = getevent(L);
	event_stop(le);
	if (le->completed) {
		ll_del(&le->clist);
		le->completed = 0;
	}
	return 0;
}

/* poll for incoming events:
   event.poll()
 */
static	int	ev_poll(lua_State *L)
{
	struct	luaevent *ev;

	/* nothing to return - wait */
	while (ll_empty(&completed)) {
		/* no events at all */
		if (event_loop(EVLOOP_ONCE) != 0)
			return 0;
	}

	ev = ll_get(completed.next, struct luaevent, clist);
	ll_del(&ev->clist);

	/* make the event stopped if it was not persistent (i.e. was oneshot) or timeout happened */
	if ((!(ev->mask & EV_PERSIST)) || (ev->ev.ev_res & EV_TIMEOUT))
		ev->started = 0;
	assert(ev->completed);
	ev->completed = 0;

	/* get the event udata's value */
	lua_pushliteral(L, EVENTAB);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_pushlightuserdata(L, ev);
	lua_rawget(L, -2);

	/* return ev, nil, nil on timeout */
	if (ev->ev.ev_res & EV_TIMEOUT)
		return 1;

	lua_pushboolean(L, ev->ev.ev_res & EV_READ);
	lua_pushboolean(L, ev->ev.ev_res & EV_WRITE);
	return 3;
}

static	luaL_reg ev_meth[] = {
	{ "add",	ev_add },
	{ "fd",		ev_fd },
	{ "mask",	ev_mask },
	{ "timeout",	ev_timeout },
	{ "oneshot",	ev_oneshot },
	{ "start",	ev_start },
	{ "stop",	ev_stop },
	{ "del",	ev_del },
	{ "poll",	ev_poll },
	{ "__gc",	ev_del },
	{ NULL, NULL }
};

LUALIB_API int luaopen_event(lua_State *L)
{
	event_init();

	lua_pushliteral(L, EVENTAB);
	lua_newtable(L);	/* our weak table */
	lua_newtable(L);	/* our meta table */
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "v"); /* weak values */
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
	

	luaL_newmetatable(L, EVHANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, ev_meth);
	luaL_register(L, "event", ev_meth);
}

