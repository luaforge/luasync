/*
 * $Id: net.c,v 1.7 2006-06-06 01:39:03 ezdy Exp $
 *
 * Network sockets cruft
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#include <fcntl.h>

#include <unistd.h>

#include "err.h"
#include "buf.h"
#include "event.h"

#define	SOCK_TCP	1
#define	SOCK_UDP	2
#define	SOCK_CONNECTED	4
#define	SOCK_ACCEPTED	8

#define ERRNIL() { err_no = errno; return 0; }

#define	NET_BUF_SIZE	4096

/* convert args to struct sockaddr_in. return 1 if success, 0 if no args and -1 on error */
static	int	str2sin(lua_State *L, int pos, struct sockaddr_in *sin)
{
	const char	*host;
	int	port;
	in_addr_t ip;

	host = lua_tostring(L, pos);

	port = luaL_optint(L, pos+1, 0);
	if (!host && !port)
		return 0;

	ip = INADDR_ANY;
	if (host) {
		ip = inet_addr(host);
		if (ip == INADDR_NONE)
			return -1;
	}
	if (port < 0 || port > 65535)
		return -1;
	sin->sin_addr.s_addr = ip;
	sin->sin_port = htons(port);
	return (host != NULL) + (port != 0);
}

/* create our socket udata and assign metatables to it */
static	inline	struct sock *mksock(lua_State *L)
{
	struct sock *s;

	s = lua_newuserdata(L, sizeof(*s));

	s->fd = -1;
	s->flags = 0;
	s->evmask = 0;
	luaL_getmetatable(L, SOCKHANDLE);
	lua_setmetatable(L, -2);

	/* put it into the registry too */
	lua_getfield(L, LUA_REGISTRYINDEX, TIMER2ID);
	lua_pushlightuserdata(L, s);
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	/* initialize events */
	ev_sinit(s);
	return s;
}


/* create tcp/udp socket */
static	int	net_ip(lua_State *L, int type, int proto)
{
	struct	sockaddr_in sin;
	int	fd;
	struct	sock *sock;

	err_no = EINVAL;

	memset(&sin, 0, sizeof(sin));

	fd = socket(AF_INET, type, proto);
	if (fd < 0)
		ERRNIL();

	sin.sin_family = AF_INET;
	switch (str2sin(L, 1, &sin)) {
		case 2:
		case 1:
			if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
				ERRNIL();
			break;
		case -1:
			return 0;
	}

	sock = mksock(L);
	sock->fd = fd;
	sock->flags = type=SOCK_STREAM?SOCK_TCP:SOCK_UDP;
	err_no = 0;
	return 1;
}

/* tcp */
static	int	net_tcp(lua_State *L)
{
	return net_ip(L, SOCK_STREAM, IPPROTO_TCP);
}

/* udp */
static	int	net_udp(lua_State *L)
{
	return net_ip(L, SOCK_DGRAM, IPPROTO_UDP);
}

/* bind to ip/port */
static	int	net_bind(lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	sockaddr_in sin;

	err_no = EINVAL;

	if (str2sin(L, 2, &sin) <= 0)
		return 0;
	if (bind(s->fd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
		ERRNIL();

	err_no = 0;
	lua_pushboolean(L, 1);
	return 1;
}

static	int	net_listen(lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	int	n = luaL_optint(L, 2, DEFAULT_LISTEN_QUEUE);

	err_no = 0;
	if (listen(s->fd, n))
		err_no = errno;
	lua_pushboolean(L, !err_no);
	return 1;
}

/* socket options handling. do not #ifdef missing options for your os,
   instead set dummy values in net.h */
static	int	net_opt(lua_State *L)
{
#define SO SOL_SOCKET
#define TCP -1
#define UDP -2
#define IP 3
#define BOOL 0
#define INT 1
#define BYTE 2
#define SPEC 3
	static	const struct {
		int	name;
		int	level;
		int	type;
	} opts[] = {
		{ SO_REUSEADDR, SO },
		{ SO_KEEPALIVE, SO },
		{ SO_LINGER, SO, SPEC }, /* special */
		{ SO_SNDBUF, SO, INT },
		{ SO_RCVBUF, SO, INT },
		{ SO_SNDLOWAT, SO, INT },
		{ SO_RCVLOWAT, SO, INT },
		{ SO_SNDTIMEO, SO, INT },
		{ SO_RCVTIMEO, SO, INT },
		{ TCP_CORK, TCP },
		{ TCP_DEFER_ACCEPT, TCP },
		{ TCP_NODELAY, TCP },
		{ TCP_MAXSEG, TCP, INT },
		{ UDP_CORK, UDP },
		{ IP_OPTIONS, IP }, 
		{ IP_HDRINCL, IP },
		{ IP_TOS, IP, BYTE },
		{ IP_TTL, IP, INT },
		{ IP_RECVOPTS, IP },
		{ IP_RECVTTL, IP }
	};
	static	const char	*opt_names[] = {
		"reuseaddr",	
		"keepalive",
		"linger",
		"sndbuf",
		"rcvbuf",
		"sndlowat",
		"rcvlowat",
		"sndtimeo",
		"rcvtimeo",
		"cork",
		"defer_accept",
		"nodelay",
		"maxseg",
		"udpcork",
		"options",
		"hdrincl",
		"tos",
		"ttl",
		"recvopts",
		"recvttl",
		NULL
	};
	int	i;
	struct	sock *s = tosock(L, 1);

	static	unsigned char byte;
	static	int	integer;
	static	int	boolean;
	static	struct	linger special;

	static	int	lens[] = { sizeof(boolean), sizeof(integer), sizeof(byte), sizeof(special) };
	static	void	*ptrs[] = { &boolean, &integer, &byte, &special };
	int	level;

	i = luaL_checkoption(L, 2, NULL, opt_names);

	level = opts[i].level;

	err_no = EINVAL;

	/* need to lookup the level */
	if (level < 0) {
		struct protoent *pe = getprotobyname(level==-1?"tcp":"udp");
		if (!pe)
			return 0;
		level = pe->p_proto;
	}

	/* op not supported */
	if (opts[i].name < 0) {
		err_no = EOPNOTSUPP;
		return 0;
	}

	err_no = 0;
	/* getsockopt */
	if (lua_isnil(L, 3)) {
		int mylen = lens[opts[i].type];

		if (getsockopt(s->fd, level, opts[i].name, ptrs[opts[i].type], &mylen) < 0)
			ERRNIL();

		switch (opts[i].type) {
			case BYTE:
				lua_pushinteger(L, byte);
				return 1;
			case INT:
				lua_pushnumber(L, integer);
				return 1;
			case BOOL:
				lua_pushboolean(L, boolean);
				return 1;
			case SPEC:
				lua_pushboolean(L, special.l_onoff);
				lua_pushinteger(L, special.l_linger);
				return 2;
		}
	}

	/* setsockopt */
	switch (opts[i].type) {
		case BYTE:
			byte = luaL_checkint(L, 2);
			break;
		case INT:
			integer = luaL_checkint(L, 2);
			break;
		case BOOL:
			boolean = lua_toboolean(L, 2);
			break;
		case SPEC:
			special.l_onoff = lua_toboolean(L, 2);
			special.l_linger = luaL_checkint(L, 3);
			break;
	}
	if (setsockopt(s->fd, level, opts[i].name, ptrs[opts[i].type], lens[opts[i].type]) < 0)
		ERRNIL();

	lua_pushboolean(L, 1);
	return 1;
}

#undef SO
#undef TCP
#undef UDP
#undef IP
#undef BOOL
#undef INT
#undef BYTE

/* set the socket into blocking/nonblocking mode */
static	int	net_nonblock(lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	int	fl;

	err_no = 0;
	fl = fcntl(s->fd, F_GETFL, 0);
	if (fl < 0)
		ERRNIL();

	if (fcntl(s->fd, F_SETFL, lua_toboolean(L, 2) ? (fl | O_NONBLOCK) : (fl & (~O_NONBLOCK))))
		ERRNIL();

	lua_pushboolean(L, 1);
	return 1;
}

/* getpeername/setpeername */
static	int	net_peername(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	sockaddr_in remote;
	socklen_t rlen = sizeof(remote);

	err_no = EINVAL;

	switch (str2sin(L, 2, &remote)) {
		case 1:
		case 2:
			/* peer can be set only for udp */
			if (!(s->flags & SOCK_UDP))
				return 0;
			memset(&remote, 0, sizeof(remote));
			remote.sin_family = AF_INET;
			if (connect(s->fd, (struct sockaddr *) &remote, sizeof(remote)))
				ERRNIL();
			lua_pushboolean(L, 1);
			return 1;
		/* invalid argument */
		case -1:
			return 0;
	}
	/* ok, being here means getpeername */
	if (getpeername(s->fd, (struct sockaddr *) &remote, &rlen))
		ERRNIL();

	lua_pushstring(L, inet_ntoa(remote.sin_addr));
	lua_pushinteger(L, ntohs(remote.sin_port));
	return 2;
}

/* accept new tcp connection */
static	int	net_accept(struct lua_State *L)
{
	struct	sock *ns, *s = tosock(L, 1);
	int	newfd;
	struct	sockaddr_in remote;
	socklen_t rlen = sizeof(remote);

	newfd = accept(s->fd, (struct sockaddr *) &remote, &rlen);
	if (newfd < 0)
		ERRNIL();

	ns = mksock(L);
	ns->flags = SOCK_TCP|SOCK_ACCEPTED|SOCK_CONNECTED;
	ns->fd = newfd;

	lua_pushstring(L, inet_ntoa(remote.sin_addr));
	lua_pushinteger(L, ntohs(remote.sin_port));
	return 3;
}

/* connect to remote host/port */
static	int	net_connect(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	sockaddr_in remote;

	err_no = EINVAL;
	if (str2sin(L, 2, &remote) != 2)
		ERRNIL();
	err_no = 0;

	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	if (connect(s->fd, (struct sockaddr *) &remote, sizeof(remote)))
		ERRNIL();
	lua_pushboolean(L, 1);
	return 1;
}

/* receive datagram */
static	int	net_recv_dgram(struct lua_State *L, struct sock *s, struct luabuf *buf, int len, struct sockaddr_in *remote)
{
	socklen_t fromlen = sizeof(*remote);
	struct	bufchain *bc;
	int	got;

	bc = buf_grab(buf, len, 1);
	got = recvfrom(s->fd, bc->raw->data + bc->start + bc->len, bc->raw->free, 0, (struct sockaddr *) remote, &fromlen);

	if (got <= 0) {
		buf_commit(buf, 0);
		err_no = errno;
		return 0;
	}
	buf_commit(buf, got);
	lua_pushinteger(L, got);
	return 0;
}

/* receive from tcp/udp socket */
static	int	net_recv(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	luabuf *buf = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	int	len = luaL_optint(L, 3, 0);
	int	exit = 0;
	int	sofar = 0;
	struct	bufchain *bc;

	if (!len) {
		exit = 1;
		len = NET_BUF_SIZE;
	}

	if (s->flags & SOCK_UDP)
		return net_recv_dgram(L, s, buf, len?len:NET_BUF_SIZE, NULL);

	err_no = 0;

	while (len > 0) {
		int got;
		bc = buf_grab(buf, MIN(len, NET_BUF_SIZE), 0);
		got = read(s->fd, bc->raw->data + bc->start + bc->len, bc->raw->free);
		if (got <= 0) {
			if (got)
				err_no = errno;
			else
				err_no = EPIPE;
			buf_commit(buf, 0);
			if (!sofar)
				return 0;
			break;
		}
		sofar += got;
		len -= got;
		buf_commit(buf, got);
		/* exit after first read */
		if (exit)
			break;
	}
	lua_pushinteger(L, sofar);
	return 1;
}

/* receive from udp socket and return remote host */
static	int	net_recvfrom(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	luabuf *buf = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	int	len = luaL_optint(L, 3, 0);
	struct	sockaddr_in remote;

	err_no = EINVAL;
	if (!(s->flags & SOCK_UDP))
		return 0;
	err_no = 0;

	if (!net_recv_dgram(L, s, buf, len?len:NET_BUF_SIZE, &remote))
		return 0;

	lua_pushstring(L, inet_ntoa(remote.sin_addr));
	lua_pushinteger(L, ntohs(remote.sin_port));
	return 3;
}

static	int	net_send_dgram(struct lua_State *L, struct sock *s, struct luabuf *buf, struct sockaddr_in *to)
{
	char	*p;
	int	got, tosend = 0;
	struct	bufchain *bc;
	llist	*ll;

	err_no = 0;
	if (!buf->len) {
		lua_pushinteger(L, 0);
		return 1;
	}

	bc = ll_get(buf->chain.next, struct bufchain, list);
	p = bc->raw->data + bc->start;
	tosend = buf->len;

	/* uh-oh. not good */
	if (buf->len > bc->len) {
		p = alloca(buf->len);
		tosend = 0;

		ll_for(buf->chain, ll) {
			bc = ll_get(ll, struct bufchain, list);
			memcpy(p + tosend, bc->raw->data + bc->start, bc->len);
			tosend += bc->len;
		}
		assert(tosend == buf->len);
	}

	got = sendto(s->fd, p, tosend, 0, (struct sockaddr *) to, sizeof(*to));
	if (got <= 0)
		ERRNIL();
	lua_pushinteger(L, got);
	return 1;
}

/* send bytes */
static	int	net_send(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	luabuf *buf = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	int	sent = 0;
	llist	*ll;

	if (s->flags & SOCK_UDP)
		return net_send_dgram(L, s, buf, NULL);


	/* sending a stream is pretty straightforward */
	ll_for(buf->chain, ll) {
		struct bufchain *bc = ll_get(ll, struct bufchain, list);
		int todo = bc->len;
		char *p = bc->raw->data + bc->start;

		while (todo > 0) {
			int got;
			got = write(s->fd, p, todo);
			if (got <= 0) {
				if (!sent)
					ERRNIL();
				err_no = errno;
				goto out;
			}
			sent += got;
			todo -= got;
		}
	}
out:;
	lua_pushinteger(L, sent);
	return 1;
}

static	int	net_sendto(struct lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	struct	luabuf *buf = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	struct	sockaddr_in sin;

	err_no = EINVAL;
	if (!(s->flags & SOCK_UDP))
		return 0;

	sin.sin_family = AF_INET;
	if (str2sin(L, 2, &sin) != 2)
		return 0;

	return net_send_dgram(L, s, buf, &sin);
}

static	int net_close(lua_State *L)
{
	struct	sock *s = tosock(L, 1);
	if (s->fd != -1) {
		ev_unset(s);
//		fprintf(stderr, "collected fd socket %p, fd %d\n", s, s->fd);
		close(s->fd);
		s->fd = -1;
	}
//	lua_pushlightuserdata(L, s);
//	lua_pushnil(L);
//	lua_rawset(L, LUA_REGISTRYINDEX);
	return 0;
}

static	luaL_reg net_meth[] = {
	{ "tcp",	net_tcp },
	{ "udp",	net_udp },
	{ "bind",	net_bind },
	{ "listen",	net_listen },
	{ "opt",	net_opt },
	{ "nonblock",	net_nonblock },
	{ "peername",	net_peername },
	{ "accept",	net_accept },
	{ "connect",	net_connect },
	{ "recv",	net_recv },
	{ "recvfrom",	net_recvfrom },
	{ "send",	net_send },
	{ "sendto",	net_sendto },
	{ "close",	net_close },
	{ "__gc",	net_close },
	{ NULL, NULL }
};

int	net_init(struct lua_State *L)
{
	luaL_newmetatable(L, SOCKHANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, net_meth);
	luaL_register(L, "net", net_meth);
	return 0;
}

