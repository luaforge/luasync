/*
 * $Id: buf.h,v 1.4 2006-05-27 03:19:21 ezdy Exp $
 * buf.h - buffer VM implementation.
 * provides primitives for operating large blobs of data,
 * appending, prepending, inserting, cutting etc.
 *
 * use this for what it was designed for: large chunks of data.
 * otherwise it's likely it will be less efficient than lua's
 * native strings.
 *
 */


#ifndef __BUF_H
#define __BUF_H

#include <stdlib.h>
#include <stdio.h>

#include <lua.h>
#include <lualib.h>
#include <assert.h>
#include <string.h>

#include "ll.h"
#include "buf.h"

#if 1
#define DEBUG(fmt...) { fprintf(stderr, fmt); fprintf(stderr, "\n"); fflush(stderr); }
#else
#define DEBUG(...)
#endif
#define BUFHANDLE "buf*"
#define	BUF_PREALLOC	512

/*************************************************************************
 * structs 
 *************************************************************************/

/* this is the low-level refcounted buffer structure */
struct	rawbuf {
	int	refcount;
	int	len;
	int	free;	/* space currently occupied */
	unsigned char	data[0];
};

/* this is the chain of rawbuf's comprising one whole buffer */
struct	bufchain {
	llist	list;
	int	start;
	int	len;
	struct	rawbuf *raw;
};

/* this is the userdata lua structure */
struct	luabuf {
	int	len;
	llist	chain;
};


/*************************************************************************
 * externs 
 *************************************************************************/
extern	int	rused, vused;
extern	int buf_init(lua_State *L);

#define BUF_HARD	1	/* raise error if not buf type, otherwise NULL */
#define	BUF_CONV	2	/* try convert from other types */

/*************************************************************************
 * rawbufs operations
 *************************************************************************/

/* allocate new raw buffer of len */
static	inline	struct rawbuf *bufr_new(int len)
{
	struct rawbuf *rb = malloc(sizeof(*rb) + len);
	rb->refcount = 1;
	rb->len = len;
	rb->free = 0;
	rused += len;
	vused += len;
	return rb;
}

/* get a reference (and increase refcount) to the rawbuf rb */
static	inline	struct rawbuf *bufr_get(struct rawbuf *rb)
{
	rb->refcount++;
	vused += rb->len + rb->free;
	return rb;
}

static	inline	struct	rawbuf *bufr_copy(struct rawbuf *rb, int pos, int len)
{
	struct	rawbuf *nr;

	assert(pos + len < rb->len);
	nr = bufr_new(len);
	nr->refcount = 1;
	nr->len = len;
	memcpy(nr->data, rb->data + pos, len);
	return nr;
}

/* put a reference to the rb, freeing it when there are no refs */
static	inline	int bufr_put(struct rawbuf *rb)
{
	assert(rb->refcount > 0);
	vused -= rb->len + rb->free;
	if (!--rb->refcount) {
		rused -= rb->len + rb->free;
		free(rb);
		return 1;
	}
	return 0;
}

/*************************************************************************
 * bufchains/block operations
 *************************************************************************/
/* clone the given buffer's chain block at specified pos & len */
static	inline struct bufchain *bufc_clone(struct bufchain *ob, int pos, int len)
{
	struct	bufchain *nb;
	DEBUG("pos=%d len=%d ob->raw->len=%d", pos, len, ob->raw->len);
	assert(pos + len <= ob->raw->len);

	/* clone the block and bump the refcount */
	nb = malloc(sizeof(*nb));
	nb->start = ob->start + pos;
	nb->len = len;
	nb->raw = bufr_get(ob->raw);
	return nb;
}

/* this will physically duplicate bufchain (and align the rawbuffer len to the bufchain's
   if needed) */
static	inline	struct bufchain *bufc_copy(struct bufchain *ob, int pos, int len)
{
	struct	bufchain *nb;
	assert(pos + len < ob->raw->len);

	/* create the block and copy the raw data */
	nb = malloc(sizeof(*nb));
	nb->start = 0;
	nb->len = len;
	nb->raw = bufr_copy(ob->raw, ob->start + pos, len);
	return nb;
}

/*************************************************************************
 * bufchains operations
 *************************************************************************/
static inline struct bufchain *buf_grab(struct luabuf *in, int len, int force)
{
	struct	bufchain *bc;

	DEBUG("grabbing %d bytes, force=%d", len, force);
	if (force || (!in->len))
		goto noavail;
	assert(!ll_empty(&in->chain));
	bc = ll_get(in->chain.prev, struct bufchain, list);
	if (!bc->raw->free)
		goto noavail;


	return bc;
noavail:
	DEBUG("not enough space available");
	bc = malloc(sizeof(*bc));
	bc->len = bc->start = 0;
	bc->raw = bufr_new(len);
	bc->raw->len = 0;
	bc->raw->free = len;
	ll_add(in->chain.prev, &bc->list);
	return bc;
}

static	inline	void buf_commit(struct luabuf *in, int len)
{
	struct	bufchain *bc;
	DEBUG("commiting %d bytes to %p (%d)", len, in, bc->raw->free);
	bc = ll_get(in->chain.prev, struct bufchain, list);
	assert(len <= bc->raw->free);
	bc->len += len;
	in->len += len;
	bc->raw->len += len;
	bc->raw->free -= len;
	if (!bc->len) {
		ll_del(&bc->list);
		free(bc);
	}
}

/* try fit some bytes to the free space at the tail of the chain.
   returns the number of bytes fitted */
static inline int	buf_tryfitbytes(struct luabuf *in, char *bytes, int len)
{
	struct	bufchain *bc;
	if (!in->len || !len)
		return 0;

	assert(in->chain.prev != &in->chain);

	bc = ll_get(in->chain.prev, struct bufchain, list);
	if (bc->raw->free) {
		if (bc->raw->free < len)
			len = bc->raw->free;
		/* the chunk must NOT overlap the free space */
		assert(bc->start + bc->len < (bc->raw->len + bc->raw->free));
		memcpy(bc->raw->data + bc->raw->len, bytes, len);
		bc->raw->len += len;
		in->len += len;
		bc->raw->free -= len;
		return len;
	}
	return 0;
}
/* just create new empty buffer */
static inline struct	luabuf *buf_new(struct lua_State *L)
{
	struct	luabuf *b = lua_newuserdata(L, sizeof(*b));
	b->len = 0;
	ll_init(&b->chain);

	luaL_getmetatable(L, BUFHANDLE);
	lua_setmetatable(L, -2);
	return b;
}


/* convert string to buffer. prealloc some bytes in the result (prealloc)
   if requested, if we're last chunk and buf_tryfitbytes might work */
static inline struct luabuf *buf_fromstring(struct lua_State *L, char *bytes, int len, int prealloc)
{
	struct	luabuf *lb = buf_new(L);
	struct	bufchain *bc = malloc(sizeof(*bc));
	int pre = prealloc?BUF_PREALLOC:0;

	bc->len = len;
	bc->start = 0;
	bc->raw = bufr_new(len + pre);
	bc->raw->len -= pre;
	bc->raw->free = pre;
	memcpy(bc->raw->data, bytes, len);

	ll_add(&lb->chain, &bc->list);
	lb->len = bc->len;
	return lb;
}

/* create new unique buffer from the original. equivalent to
   buf_sub(in, 0, 0).
   
 */
static inline struct	luabuf *buf_dup(struct lua_State *L, struct luabuf *in)
{
	llist *i;
	struct	luabuf *out = buf_new(L);

	ll_for(in->chain, i) {
		struct bufchain *bc = ll_get(i, struct bufchain, list);
		struct bufchain *clone = bufc_clone(bc, 0, bc->len);
		ll_add(out->chain.prev, &clone->list);
		out->len += bc->len;
	}
	assert(in->len == out->len);
	return out;
}

/*
 * this will find a bufchain at a specific position in luabuf. returns NULL if
 * the index is out of range.
 * the linear complexity of O(n/2) could be somewhat reduced up to
 * O(log n) by using redblack trees or similiar, but that'd be overkill
 * for n being about 50-100 max anyway. however, volunteers are welcome.
 *
 * for inserting/splitting:
 * if retpos == nb->len then APPEND to the returned chunk, if 0
 * then prepend, otherwise split.
 */
static inline struct bufchain *buf_findpos(struct luabuf *buf, int pos, int *retpos)
{
	llist	*left, *right;
	struct	bufchain *leftb, *rightb;
	int	leftc, rightc;

	right = left = &buf->chain;
	if ((pos < 0) || (pos > buf->len))
		return NULL;

	leftc = 0;
	rightc = buf->len;


	/* this will eventually end up in endlesss loop ;) */
	while (1) {
		right = right->prev;
		rightb = ll_get(right, struct bufchain, list);
		rightc -= rightb->len;
		if (rightc <= pos) {
			*retpos = pos - rightc;
			return rightb;
		}

		left = left->next;
		leftb = ll_get(left, struct bufchain, list);
		leftc += leftb->len;
		if (leftc >= pos) {
			*retpos = leftb->len - (leftc - pos);
			return leftb;
		}
	}
	return NULL;
}

/* get a buffer from given pos of the stack. if hard is nonzero
   we will throw exception when there is nothing resembling
   our buffer. also we try to convert to buffer if there's
   __tobuffer or __tostring */
static	inline	struct	luabuf *lua_tobuf(struct lua_State *L, int pos, int flags)
{
	struct	luabuf *lb;
	DEBUG("doing tobuf @ %d/%d", pos, flags);
	lua_getfield(L, LUA_REGISTRYINDEX, BUFHANDLE);
	if (lua_getmetatable(L, pos)) {
		DEBUG("the thing has metatable");
		if (lua_rawequal(L, -1, -2)) {
			lb = (void *) lua_touserdata(L, pos);
			if (lb)
				return lb;
		}
		DEBUG("but not ours");

		/* ok. it seems to be something else */
		lua_pushstring(L, "__tobuf");
		lua_rawget(L, -2);
		if (!lua_isnil(L, -1)) {
			lua_pushvalue(L, pos);
			lua_call(L, 1, 1);
			if (!lua_isnil(L, -1)) {
				/* converting was successful */
				if (lua_getmetatable(L, -1) && lua_rawequal(L, -1, -4)) {
					lb = lua_touserdata(L, -2);
					lua_pop(L, 4); /* meta, meta, retval, meta*/
					return lb;
				}
			}
			lua_pop(L, 3); /* meta, meta, retval */
			goto out;
		}
		DEBUG("__tobuf not found");
		/* and it does not even provide __tobuf :( pity */
		lua_pop(L, 1); /* meta from 1st getmeta */
	}

	lua_pop(L, 1); /* getfield result */
	if (flags & BUF_CONV) {
		size_t l;
		char *s;

		DEBUG("trying to convert");

		s = (char *) lua_tolstring(L, pos, &l);
		if (s)
			return buf_fromstring(L, s, l, 0);
	}
out:
	if (flags & BUF_HARD)
		luaL_typerror(L, pos, BUFHANDLE);
	return NULL;
}


#endif
