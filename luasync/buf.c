/*
 * $Id: buf.c,v 1.6 2006-05-29 02:41:30 ezdy Exp $
 * buffer VM implementation.
 * provides primitives for operating large blobs of data,
 * appending, prepending, inserting, cutting etc.
 *
 * use this for what it was designed for: large chunks of data.
 * otherwise it's likely it will be less efficient than lua's
 * native strings.
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "ll.h"
#include "buf.h"

int	rused = 0;	/* raw memory used */
int	vused = 0;	/* virtual memory used */

/*************************************************************************
 * lua visible operations
 *************************************************************************/
/* allocate new buffer */
static	int	bufL_new(struct lua_State *L)
{
	buf_new(L);
	return 1;
}

/* buf.sub(buf[, start[, len]]). this returns a NEW buffer. */
static	int	bufL_sub(struct lua_State *L)
{
	struct	luabuf *in, *out;
	struct	bufchain *bc, *bn;
	int	start, len, clen, rp;

	/* get args */
	lua_settop(L, 3);
	in = (void *) lua_tobuf(L, 1, BUF_HARD|BUF_CONV);
	out = buf_new(L);
	start = luaL_optint(L, 2, 0);

	/* some sanity */
	if (start < 0)
		start = 0;
	if (start > in->len)
		start = in->len;
	len = luaL_optint(L, 3, in->len - start);
	if (len + start > in->len)
		len = in->len - start;
	assert((len + start) <= in->len); assert(len >= 0); assert(start >= 0);

	/* find the appropiate chunk */
	if (!(bc = buf_findpos(in, start, &rp)))
		return 0;

	clen = bc->len - rp;
	while (len) {
		while (!clen) {
			rp = 0;
			/* this is the end of list? how come? */
			if (bc->list.next == &in->chain) {
				fprintf(stderr, "shouldnothappen: list too short?");
				return 1;
			}
			bc = ll_get(bc->list.next, struct bufchain, list);
			clen = bc->len;
		}
		if (clen > len)
			clen = len;
		bn = bufc_clone(bc, rp, clen);
		ll_add(out->chain.prev, &bn->list);
		out->len += clen;
		len -= clen;
		clen = 0;
	}
	return 1;
}

static	int	bufL_dup(struct lua_State *L)
{
	struct	luabuf *orig = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	buf_dup(L, orig);
	return 1;
}

/* buf.rm(buf, start, len)
 * use 8< or primitive force to cut off some part
 * of buffer. this modifies the ORIGINAL BUFFER
 * and just returns it.
 * this is by no means the most obfuscated algorithm,
 * and thus probably the buggy one.
 */
static	int	bufL_rm(struct lua_State *L)
{
	struct	luabuf *in;
	llist	*bcnext;
	struct	bufchain *bc, *bn;
	int	start, olen, len, rp;

	/* get args */
	lua_settop(L, 3);
	in = (void *) lua_tobuf(L, 1, BUF_HARD|BUF_CONV);
	start = luaL_optint(L, 2, 0);

	/* some sanity */
	if (start < 0)
		start = 0;
	if (start > in->len)
		start = in->len;
	len = luaL_optint(L, 3, in->len - start);
	if (len + start > in->len)
		len = in->len - start;
	assert((len + start) <= in->len); assert(len >= 0); assert(start >= 0);

	/* find the appropiate chunk */
	if (!(bc = buf_findpos(in, start, &rp)))
		return 0;

	olen = len;
	bcnext = bc->list.next;
	while (len > 0) {
		DEBUG("rp=%d, bclen=%d", rp, bc->len);
		if (rp == bc->len) {
			bc = ll_get(bcnext, struct bufchain, list);
			if (bcnext == &in->chain) {
				fprintf(stderr, "shouldnothappen: list too short?");
				goto out;
			}
			bcnext = bc->list.next;
			rp = 0;
		}

		assert(rp <= bc->len);
		/* it is possible to cut out this whole block if
                   rp is 0 and len >= whole block */
		if ((rp == 0) && (len >= bc->len)) {
			len -= bc->len;
			bufr_put(bc->raw);
			ll_del(&bc->list);
			free(bc);
			continue;
		}
		/* we can fall in here only in 2 cases:
			1. rp > 0, which means we need to preserve 0 up to rp of buffer
			2. rp+len < bc->len which means that offset rp+len up to bc->len
		           needs to be preserved
		*/
		assert((rp > 0) || (rp+len < bc->len));
		/* 1st case||2nd case */
		if (rp > 0) {
			/* 2nd case first as this might append to us.
			   this means both cases happened and we've to split
			   the struct into two */
			if (rp + len < bc->len) {
				bn = bufc_clone(bc, rp+len, bc->len - (rp+len));
				ll_add(&bc->list, &bn->list);
			}

			len -= bc->len - rp;
			bc->len = rp;
		} else {
			DEBUG("cut case 2");
			/* it is only the 2nd case. in that case reuse old block struct */
			bc->start = bc->start + rp+len;
			bc->len = bc->len - (rp+len);
			len = 0;
		}
	}
out:
	DEBUG("in->len=%d olen=%d", in->len, olen);
	assert(in->len >= olen);
	in->len -= olen;
	lua_pushinteger(L, olen);
	return 1;
}

/* this is equal to bufL_rm, except that
   it returns the cutted part as a new buffer */
static	int	bufL_cut(struct lua_State *L)
{
	struct	luabuf *in, *out;
	llist	*bcnext;
	struct	bufchain *bc, *bn;
	int	start, olen, len, rp;

	/* get args */
	lua_settop(L, 3);
	in = (void *) lua_tobuf(L, 1, BUF_HARD|BUF_CONV);
	out = buf_new(L);
	start = luaL_optint(L, 2, 0);

	/* some sanity */
	if (start < 0)
		start = 0;
	if (start > in->len)
		start = in->len;
	len = luaL_optint(L, 3, in->len - start);
	if (len + start > in->len)
		len = in->len - start;
	assert((len + start) <= in->len); assert(len >= 0); assert(start >= 0);

	/* find the appropiate chunk */
	if (!(bc = buf_findpos(in, start, &rp)))
		return 0;

	olen = len;

	bcnext = bc->list.next;
	while (len > 0) {
		DEBUG("rp=%d, bclen=%d", rp, bc->len);
		if (rp == bc->len) {
			bc = ll_get(bcnext, struct bufchain, list);
			if (bcnext == &in->chain) {
				fprintf(stderr, "shouldnothappen: list too short?");
				goto out;
			}
			bcnext = bc->list.next;
			rp = 0;
		}

		assert(rp <= bc->len);
		/* it is possible to cut out this whole block if
                   rp is 0 and len >= whole block */
		if ((rp == 0) && (len >= bc->len)) {
			len -= bc->len;
			ll_del(&bc->list);

			ll_add(out->chain.prev, &bc->list);
			out->len += bc->len;
			DEBUG("cut bn->len=%d, out->len=%d, bc=%p", bc->len, out->len, bc);
			rp = bc->len;
			continue;
		}
		/* we can fall in here only in 2 cases:
			1. rp > 0, which means we need to preserve 0 up to rp of buffer
			2. rp+len < bc->len which means that offset rp+len up to bc->len
		           needs to be preserved
		*/
		assert((rp > 0) || (rp+len < bc->len));

		/* getting the cutted part is quite easy .. */
		bn = bufc_clone(bc, rp, (rp+len)>(bc->len-rp)?(bc->len-rp):rp+len);
		ll_add(out->chain.prev, &bn->list);
		out->len += bn->len;
		DEBUG("cut bn->len=%d, out->len=%d, bn=%p", bn->len, out->len, bn);

		/* 1st case||2nd case */
		if (rp > 0) {
			/* 2nd case first as this might append to us.
			   this means both cases happened and we've to split
			   the struct into two */
			if (rp + len < bc->len) {
				bn = bufc_clone(bc, rp+len, bc->len - (rp+len));
				ll_add(&bc->list, &bn->list);
			}

			len -= bc->len - rp;
			bc->len = rp;
		} else {
			DEBUG("cut case 2");
			/* it is only the 2nd case. in that case reuse old block struct */
			bc->start = bc->start + rp+len;
			bc->len = bc->len - (rp+len);
			len = 0;
		}
	}
out:
	DEBUG("in->len=%d olen=%d, out->len=%d", in->len, olen, out->len);
	assert(olen == out->len);
	assert(in->len >= olen);
	in->len -= olen;
	return 1;
}


/*
 * Insert something somewhere in the buffer
 * buf.insert(buf, pos, what)
 * this does modify the original buffer 'buf'!
 */
static	int	bufL_insert(lua_State *L)
{
	int	pos, rp;
	struct	luabuf *in, *what = NULL;
	struct	bufchain *bc, *nb;
	char	*whats = NULL;
	int	whatslen = 0;

	lua_settop(L, 3);
	pos = luaL_checkint(L, 2);
	in = lua_tobuf(L, 1, BUF_HARD|BUF_CONV);

	lua_getfield(L, LUA_REGISTRYINDEX, BUFHANDLE);

	/* if it is buffer, get it */
	if (lua_getmetatable(L, 3) && lua_rawequal(L, -1, -2)) {
		what = lua_touserdata(L, 3);
	} else {
		/* it is something else. try convert */
		whats = (void *) luaL_checklstring(L, 3, (size_t *)&whatslen);
		/* alright it is string. we might try some fun though: */
		if (pos == in->len) {
			int i;
			i = buf_tryfitbytes(in, whats, whatslen);
			if (i == whatslen)
				return 1;
			whats += i;
			whatslen -= i;
		}
	}
	lua_settop(L, 1);

	/* string empty, do nothing */
	if ((!what && !whatslen) || ((what) && (!what->len)))
		return 1;
	if (!what) {
		what = buf_fromstring(L, whats, whatslen, pos == in->len);
	} else {
		/* create our private copy of the chain being appended */
		what = buf_dup(L, what);
	}

	/* alright. find the pos */
	if (!(bc = buf_findpos(in, pos, &rp)))
		return 0;

	/* simple case. pos points to the beggining of this chunk, thus
	   we will append after the previous */
	if (rp == 0) {
		ll_addlist(bc->list.prev, &what->chain);
		goto out;
	}

	/* 2nd simple case. pos points to the end of this chunk. just
	   append to it */
	if (rp == bc->len) {
		ll_addlist(&bc->list, &what->chain);
		goto out;
	}

	assert(rp < bc->len);
	/* other cases means we have to split the current chunk into two */
	nb = bufc_clone(bc, rp, bc->len - rp);
	bc->len -= nb->len;

	/* append the cloned block to the data inserted*/
	ll_add(what->chain.prev, &nb->list);

	/* append the data inserted */
	ll_addlist(&bc->list, &what->chain);

	assert(nb->len > 0 && bc->len > 0 && (bc->len + nb->len) <= bc->raw->len);
out:
	/* we must clear the chain in the dupped bufchain, otherwise
           our entries we just given to someone else would get garbage-collected. */
	LL_CLEAR(what->chain);
	in->len += what->len;
	lua_pushinteger(L, what->len);
	what->len = 0;
	return 1;
}

/* convert our buffer to string */
static	int	bufL_tostring(lua_State *L)
{
	llist *i;
	int	count = 0;
	struct luabuf *in = (void *) lua_tobuf(L, 1, BUF_HARD);

	ll_for(in->chain, i) {
		struct bufchain *bc = ll_get(i, struct bufchain, list);
		DEBUG("pushing %d bytes at pos %d", bc->len, bc->start);
		lua_pushlstring(L, bc->raw->data + bc->start, bc->len);
		count++;
	}

	/* lua will push empty string when count is 0 */
	lua_concat(L, count);
	return 1;
}

/*
 * this will append several other buffers to the first one:
 * buf.append(to, one1, one2, one3)
 * return val is buffer 'to, buffer 'TO' will be modifed.
 */
static	int	bufL_append(lua_State *L)
{
	struct	luabuf *to;
	int	i, narg = lua_gettop(L);
	int	appended = 0;

	/* no args => nothing */
	if (!narg) {
		lua_pushinteger(L, 0);
		return 1;
	}

	to = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	for (i = 2; i <= narg; i++) {
		struct luabuf *lb;
		struct luabuf *dup;
		if ((i == narg) && lua_isstring(L, i)) {
			int l;
			char *s = (char *) luaL_checklstring(L, i, (size_t *) &l);
			lb = buf_fromstring(L, s, l, 1);
		} else
			lb = lua_tobuf(L, i, BUF_CONV|BUF_HARD);
		dup = buf_dup(L, lb);
		DEBUG("appending %d", lb->len);
		/* and append the duplicate to the tail */
		ll_addlist(to->chain.prev, &dup->chain);
		LL_CLEAR(dup->chain);
		to->len += dup->len;
		appended += dup->len;
		dup->len = 0;
		lua_pop(L, 1); /* remove the duplicate */
	}
	DEBUG("result %d %d", to->len, appended);
	lua_pushinteger(L, appended);
	return 1;

}

/*
 * this will prepend several other buffers to the first one:
 * buf.prepend(to, one1, one2, one3)
 * return val is buffer 'to, buffer 'TO' will be modifed.
 * buf.prepend("a", "b", "c") = "cba"
 */
static	int	bufL_prepend(lua_State *L)
{
	struct	luabuf *to;
	int	i, narg = lua_gettop(L);
	int	prepended = 0;

	/* no args => nothing */
	if (!narg) {
		lua_pushinteger(L, 0);
		return 1;
	}

	to = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	for (i = 2; i <= narg; i++) {
		struct luabuf *lb = lua_tobuf(L, i, BUF_CONV|BUF_HARD);
		struct luabuf *dup = buf_dup(L, lb);

		/* and append the duplicate */
		ll_addlist(&to->chain, &dup->chain);
		LL_CLEAR(dup->chain);
		to->len += dup->len;
		prepended += dup->len;
		dup->len = 0;
		lua_pop(L, 1); /* remove the duplicate */
	}

	lua_pushinteger(L, prepended);
	return 1;
}

/* this will concat several buffers together:
 buf.concat(a,b,c,d...)
 it will return NEW buffer leaving all it's arguments
 unmodified. this is requiered behaviour for __concat metamethod.
 */
static	int	bufL_concat(lua_State *L)
{
	struct	luabuf *to;
	int	i, narg = lua_gettop(L);

	DEBUG("top=%d", narg);
	to = buf_new(L);
	for (i = 1; i <= narg; i++) {
		struct luabuf *lb = lua_tobuf(L, i, BUF_CONV|BUF_HARD);
		struct luabuf *dup = buf_dup(L, lb);

		/* and append the duplicate to the tail */
		ll_addlist(to->chain.prev, &dup->chain);
		LL_CLEAR(dup->chain);
		to->len += dup->len;
		DEBUG("appended %d bytes, to->len=%d", dup->len, to->len);
		dup->len = 0;
		lua_pop(L, 1); /* remove the duplicate */
	}

	/* and return the new buffer */
	lua_settop(L, narg+1);
	return 1;
}

/* this will just return buffer's length */
static	int	bufL_len(lua_State *L)
{
	struct	luabuf *lb = lua_tobuf(L, 1,  BUF_CONV|BUF_HARD);
	lua_pushnumber(L, lb->len);
	return 1;
}

/* soft equality test. instead of actual comparing
   we'll just run thru the bufchains and see if they're same */
static	int	bufL_eq(lua_State *L)
{
	struct	luabuf *b1 = lua_tobuf(L, 1, BUF_HARD);
	struct	luabuf *b2 = lua_tobuf(L, 1, BUF_HARD);
	llist	*i1, *i2;
	struct	bufchain *c1, *c2;
	lua_pushboolean(L, 1);

	/* they're same object */
	if (b1 == b2)
		return 1;

	/* len mismatch. definitively not same */
	if (b1->len != b2->len)
		return 0;

	i2 = b2->chain.next;
	ll_for(b1->chain, i1) {
		/* end of b2 */
		if (i2 == &b2->chain)
			return 0;

		c1 = ll_get(i1, struct bufchain, list);
		c2 = ll_get(i2, struct bufchain, list);
		/* mismatch */
		if ((c1->raw != c2->raw) || (c1->len != c2->len) || (c1->start != c2->start))
			return 0;
		i2 = i2->next;
	}
	/* both must be at the end */
	if ((i1 == &b1->chain) && (i2 == &b2->chain))
		return 1;
	return 0;
}

/* get byte from given index */
static	int	bufL_index(lua_State *L)
{
	struct	luabuf *lb = lua_tobuf(L, 1, BUF_HARD);
	struct	bufchain *bc;
	int	pos, idx = luaL_checkint(L, 2);
	if (idx < 0 || idx >= lb->len)
		luaL_argerror(L, 2, "index out of range");
	bc = buf_findpos(lb, idx, &pos);
	if (pos == bc->len) {
		if (bc->list.next == &lb->chain)
			luaL_argerror(L, 2, "index out of range");
		bc = ll_get(bc->list.next, struct bufchain, list);
	}
	assert(bc->start + pos < bc->raw->len);
	lua_pushinteger(L, bc->raw->data[bc->start + pos]);
	return 1;
}

/* newindex(buf,idx,val) */
static	int	bufL_newindex(lua_State *L)
{
	struct	luabuf *lb = lua_tobuf(L, 1, BUF_HARD);
	struct	bufchain *bc;
	int	pos, idx = luaL_checkint(L, 2);
	int	val = luaL_optint(L, 3, ((unsigned char *) luaL_checkstring(L, 3))[0]);
	if (idx < 0 || idx >= lb->len)
		luaL_argerror(L, 2, "index out of range");
	bc = buf_findpos(lb, idx, &pos);
	if (pos == bc->len) {
		if (bc->list.next == &lb->chain)
			luaL_argerror(L, 2, "index out of range");
		bc = ll_get(bc->list.next, struct bufchain, list);
	}
	/* the bufchain is shared by other luabufs, we must copy it */
	if (bc->raw->refcount > 1) {
		bufr_put(bc->raw);
		bc->raw = bufr_copy(bc->raw, bc->start, bc->len);
		bc->start = 0;
	}
	/* and finally write the byte */
	assert(bc->start + pos < bc->raw->len);
	bc->raw->data[bc->start + pos] = val;
	return 0;
}

/* free the buffer */
static	int	bufL_gc(lua_State *L)
{
	llist	*i, *s;
	int	len = 0;
	struct	luabuf *lb = lua_tobuf(L, 1, BUF_HARD);
	ll_forsafe(lb->chain, i, s) {
		struct bufchain *bc = ll_get(i, struct bufchain, list);
		DEBUG("bc->len=%d, bc=%p", bc->len, bc);
		len += bc->len;
		bufr_put(bc->raw);
		free(bc);
	}
	DEBUG("lb->len = %d, len=%d", lb->len, len);
	assert(lb->len == len);
	return 0;
}

int	bufL_memstat(lua_State *L)
{
	lua_pushinteger(L, vused);
	lua_pushinteger(L, rused);
	return 2;
}

#define MIN(x,y) ((x)<(y)?(x):(y))

/* atomic compare */
static	inline	int subcomp(struct luabuf *big, struct luabuf *small, struct bufchain *bigpos, int bigoff)
{
	struct	bufchain *smallpos = ll_get(small->chain.next, struct bufchain, list);
	int	tocmp, smalloff = 0;
	while (1) {
		char	*cmpa, *cmpb;
		/* small went over the offset. go to next chunk */
		if (smalloff >= smallpos->len) {
			if (smallpos->list.next == &small->chain) /* we are at the end! */
				return 1;
			smallpos = ll_get(smallpos->list.next, struct bufchain, list);
			smalloff = 0;
		}

		/* big went over the offset. go to next chunk */
		if (bigoff >= bigpos->len) {
			assert(bigpos->list.next != &big->chain);
			bigpos = ll_get(bigpos->list.next, struct bufchain, list);
			bigoff = 0;
		}

		tocmp = MIN(bigpos->len - bigoff, smallpos->len - smalloff);
		cmpa = bigpos->raw->data + bigpos->start + bigoff;
		cmpb = smallpos->raw->data + smallpos->start + smalloff;
		DEBUG("tocmp=%d cmpa=%p cmpb=%p", tocmp, cmpa, cmpb);
		if ((cmpa != cmpb) && memcmp(cmpa, cmpb, tocmp)) {
			DEBUG("mismatch!");
			return 0; /* not same */
		}
		DEBUG("bigoff=%d, matched %d!", bigoff, tocmp);
		bigoff += tocmp;
		smalloff += tocmp;

		DEBUG("bigoff=%d smalloff=%d, smallpos->len=%d, bigpos->len=%d", bigoff, smalloff, smallpos->len, bigpos->len);
	}
}

/* buf.find(buf, what, start, len) */
static	int	bufL_find(lua_State *L)
{
	struct	luabuf *big = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	struct	luabuf *small = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	struct	bufchain *bc;
	int	bcpos, lpos, limit;
	int	start, len;

	start = luaL_optint(L, 3, 0);
	len = luaL_optint(L, 4, big->len);

	/* adjust len if too high */
	if (start + len > big->len)
		len = big->len - start;

	/* common cases for no match */
	if (start < 0 || len < 0 || !big->len || !small->len || len < small->len)
		return 0;

	limit = start + (len - small->len); /* position limit within big */

	bc = buf_findpos(big, start, &lpos);
	for (bcpos = start; bcpos <= limit; bcpos++) {
		if (lpos >= bc->len) {
			bc = ll_get(bc->list.next, struct bufchain, list);
			lpos = 0;
		}
		if (subcomp(big, small, bc, lpos)) {
			lua_pushinteger(L, bcpos);
			return 1;
		}
		lpos++;
	}
	return 0;
}

/* buf.cmp(a, b[, pos]) */
static	int	bufL_cmp(lua_State *L)
{
	struct	luabuf *big = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	struct	luabuf *small = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	struct	bufchain *bc;
	int	rp, pos = luaL_optint(L, 3, 0);

	lua_pushboolean(L, 1);
	if (pos < 0)
		pos = big->len + pos;

	if (pos + small->len > big->len)
		pos = big->len - small->len;

	if (pos < 0)
		pos = 0;

	if ((big->len == 0) || (small->len == 0) || (big->len < small->len))
		return 0;
	bc = buf_findpos(big, pos, &rp);
	if (!bc)
		return 0;
	return subcomp(big, small, bc, rp);
}

static	luaL_reg buf_meth[] = {
	{ "new",	bufL_new },
	{ "sub",	bufL_sub },
	{ "dup",	bufL_dup },
	{ "cut",	bufL_cut },
	{ "rm",		bufL_rm },
	{ "insert", 	bufL_insert },
	{ "append", 	bufL_append },
	{ "prepend", 	bufL_prepend },
	{ "concat", 	bufL_concat },
	{ "find",	bufL_find },
	{ "cmp",	bufL_cmp },
	{ "peek",	bufL_index },
	{ "poke",	bufL_newindex },
	{ "memstat", 	bufL_memstat },
	{ "__tostring", bufL_tostring },
	{ "__add", 	bufL_concat },
	{ "__concat", 	bufL_concat },
	{ "__len", 	bufL_len },
	{ "__eq", 	bufL_eq },
	{ "__gc",	bufL_gc },
	{ NULL, NULL }
};

int buf_init(lua_State *L)
{
	luaL_newmetatable(L, BUFHANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, buf_meth);
	luaL_register(L, "buf", buf_meth);
	return 0;
}

