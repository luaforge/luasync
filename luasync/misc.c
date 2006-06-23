/*
 * $Id: misc.c,v 1.4 2006-06-23 01:38:59 ezdy Exp $
 *
 * various utilities for dealing with the outside world
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <fcntl.h>

#include <unistd.h>
#include <stdint.h>

#include "err.h"
#include "buf.h"

#undef DEBUG

#if 0
#define DEBUG(fmt...) { fprintf(stderr, fmt); fprintf(stderr, "\n"); fflush(stderr); }
#else
#define DEBUG(...)
#endif

#define _APPEND(ptr,ln) { \
	DEBUG("appending %d bytes", ln); \
	n = buf_tryfitbytes(buf, ptr, ln); \
	if (n < ln) { \
		struct luabuf *nb; \
		DEBUG("incomplete fit,%d/%d",n,ln); \
		nb = buf_fromstring(L, ptr + n, ln - n, 1); \
		ll_add(buf->chain.prev, nb->chain.next); \
		LL_CLEAR(nb->chain); \
		buf->len += nb->len; nb->len = 0; \
		lua_pop(L, 1); \
	} \
	done += ln; \
}
#define APPEND(ptr,ln) \
	if (bytepos) { \
		_APPEND(&byte, 1); \
		byte = bytepos = 0; \
	} \
	_APPEND(ptr, ln)

#define APPENDBIT(bit) { \
		DEBUG("appending bit %d", (bit)&1); \
		byte |= ((bit) & 1)<<bytepos; \
		bytepos++; \
		if (bytepos == 8) { \
			_APPEND(&byte, 1); \
			byte = bytepos = 0; \
		} \
	}

#define DO_ARG(chr,typ,len) \
	case chr: { \
		typ val = luaL_checknumber(L, pos); \
		char out[len]; \
		if (!(endian)) { \
			int i; \
			for (i = 0; i < len; i++) \
				out[i] = val>>(i*8); \
		} else { \
			for (i = 0; i < len; i++) \
				out[i] = val>>(((len-1)*8)-i*8); \
		} \
		pos++; \
		APPEND(out, len); \
		break; \
	}

/*
little:
|0...7|8...15|16..23|24..31|
big:
|32..24|23..16|15..8|7..0|

 */


int	misc_pack(lua_State *L)
{
	struct	luabuf *buf = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	const char	*fmt = luaL_checkstring(L, 2);
	char	c;
	int	pos = 3;
	int	repeat = 0;
	int	endian = 1;
	int	done = 0;
	int n, i;
	unsigned char	byte, bytepos;

	byte = bytepos = 0;

	while ((c = *fmt++)) {
		uint64_t b;
		const char *str;
		char *tmp;

		if (c >= '0' && c <= '9') {
			repeat = repeat * 10 + (c-'0');
			continue;
		}

		if (c == 'S') {
			if (!repeat)
				luaL_argerror(L, 2, "'S' must be prefixed by char count");
			str = luaL_checklstring(L, pos, (size_t *)&n);
			pos++;
			tmp = alloca(repeat);
			memset(tmp, 0, repeat);
			memcpy(tmp, str, n<repeat?n:repeat);
			APPEND(tmp, repeat);
			repeat = 0;
			continue;
		}
		if (!repeat)
			repeat++;

		/* arbitrary-bitwide numbers are tricky ... */
		if (c == 'n') {
			uint64_t v = luaL_checknumber(L, pos);
			unsigned char *pt = (void *) &v;
			int bcount = (repeat+7)/8;
			pos++;
			v &= (1<<(repeat))-1;
			/* little endian is trivial */
			if (!endian) {
				int	i;
				for (i = 0; i < repeat; i++) {
					APPENDBIT((uint32_t)(v >> i))
				}
			} else {
				int max, j, i;
				/* where the big endian isnt.. */
				pt += sizeof(v) - bcount;
				max = 8 - (bcount*8-repeat);
				for (i = 0; i < bcount; i++) {
					DEBUG("i=%d,max=%d",i,max);
					for (j=0;j<max;j++) {
						APPENDBIT(pt[i] >> j)
					}
					max = 8;
				}
			}
			repeat = 0;
			continue;
		}

		while (repeat--) switch (c) {
			case '<':
				endian = 0;
				break;
			case '>':
				endian = 1;
				break;
			case 'x':
				b = 0;
				APPEND(((char*)&b), 1);
				break;
			case 'c':
				str = luaL_checkstring(L, pos);
				pos++;
				APPEND(((char*)str), 1);
				break;
			case 's': {
				size_t sl;
				str = luaL_checklstring(L, pos, &sl);
				pos++;
				APPEND(((char*)str), (sl));
				break;
			}
			DO_ARG('b', signed char, 1);
			DO_ARG('B', unsigned char, 1);
			DO_ARG('h', int16_t, 2);
			DO_ARG('H', uint16_t, 2);
			DO_ARG('i', int32_t, 4);
			DO_ARG('I', uint32_t, 4);
			DO_ARG('l', int64_t, 8);
			DO_ARG('L', uint64_t, 8);
		}
		repeat = 0;
		DEBUG("done %d bytes, bytepos=%d", done, bytepos);
	}
	if (bytepos)
		_APPEND(&byte, 1);
	DEBUG("OUT: done %d bytes, bytepos=%d", done, bytepos);
	lua_pushinteger(L, done);
	return 1;
}

#undef DO_ARG
#undef APPEND
#undef _APPEND

#define MIN(x,y) ((x)<(y)?(x):(y))

#define GRAB(amount) { \
	bytepos = 8; \
	DEBUG("grabbing %d", amount); \
	if (pos+amount > bc->len) { \
		char *tmp = alloca(amount); \
		int am = amount; \
		p = tmp; \
		while (am > 0) { \
			int todo = MIN(am, bc->len - pos); \
			memcpy(tmp, bc->raw->data + bc->start + pos, todo); \
			pos += todo; \
			tmp += todo; \
			am -= todo; \
			if (pos >= bc->len) { \
				/* this is fatal */ \
				if (bc->list.next == &buf->chain) { \
					if (am) \
						return lua_gettop(L)-2; \
					quit = 1; \
					break; \
				} \
			/*	bc = ll_get(bc->list.next, struct bufchain, list);*/ \
				pos = 0; \
			} \
		} \
	} else { \
		p = bc->raw->data + bc->start + pos; \
		pos += amount; \
		if (pos >= bc->len) { \
			if (bc->list.next != &buf->chain) \
				bc = ll_get(bc->list.next, struct bufchain, list); \
			else \
				quit = 1; \
		} \
	} \
}

#define GRABIT(to) \
	DEBUG("grabbing one bit, bytepos=%d", bytepos); \
	if (bytepos == 8) { \
		GRAB(1); \
		byte = *p; \
		bytepos = 0; \
	} \
	to = (byte >> bytepos) & 1; \
	bytepos++;


#define DO_ARG(chr,typ,ln) \
	case chr: { \
		typ *val; \
		char out[ln]; \
		GRAB(ln); \
		val = (void*)p; \
		if (!(endian)) { \
			int i; \
			for (i = 0; i < ln; i++) \
				out[i] = *val>>(i*8); \
		} else { \
			for (i = 0; i < ln; i++) \
				out[i] = *val>>(((ln-1)*8)-i*8); \
		} \
		lua_pushnumber(L, *((typ *) (out))); \
		break; \
	}

int	misc_unpack(lua_State *L)
{
	struct	luabuf *buf = lua_tobuf(L, 1, BUF_CONV|BUF_HARD);
	const char	*fmt = luaL_checkstring(L, 2);
	int	start = lua_tointeger(L, 3);
	char	c, *p;
	int	quit = 0;
	struct	bufchain *bc;
	int	pos = 0;
	int	repeat = 0;
	int	endian = 1;
	int	i;
	unsigned char byte, bytepos = 8;

	byte = 0;

	lua_settop(L, 2);

	if (ll_empty(&buf->chain)) {
		DEBUG("empty buffer, nothing to unpack");
		return 0;
	}

	bc = buf_findpos(buf, start, &pos);
	DEBUG("complete fmt: %s", fmt);
	while ((c = *fmt++)) {
		DEBUG("fmt chr=%c", c);
		if (c >= '0' && c <= '9') {
			repeat = repeat * 10 + (c-'0');
			continue;
		}

		if (quit && c != 'n')
			break;
		if (c == 'S') {
			if (!repeat)
				luaL_argerror(L, 2, "'S' must be prefixed by char count");
			GRAB(repeat);
			lua_pushlstring(L, p, repeat);
			repeat = 0;
			continue;
		}
		if (!repeat)
			repeat++;
		if (c == 'n') {
			uint64_t v = 0;
			unsigned char *pt = (void *) &v;
			int bcount = (repeat+7)/8;

			if ((quit) && (bytepos + repeat > 8))
				break;

			DEBUG("UNPACKing %d bits", repeat);
			/* little endian is trivial */
			if (!endian) {
				int	i;
				for (i = 0; i < repeat; i++) {
					int to;
					GRABIT(to);
					v |= to << i;
				}
			} else {
				int max, j, i;
				/* where the big endian isnt.. */
				pt += sizeof(v) - bcount;
				max = 8 - (bcount*8-repeat);
				for (i = 0; i < bcount; i++) {
					DEBUG("i=%d,max=%d",i,max);
					for (j=0;j<max;j++) {
						int to;
						GRABIT(to)
						pt[i] |= to << j;
					}
					max = 8;
				}
			}
			lua_pushnumber(L, v);
			DEBUG("converted number to %lld, quit=%d", v, quit);
			repeat = 0;
			continue;
		}

		while ((!quit) && (repeat--)) switch (c) {
			case '<':
				endian = 0;
				break;
			case '>':
				endian = 1;
				break;
			case 'x':
				GRAB(1);
				break;
			case 'c': {
				GRAB(1);
				lua_pushlstring(L, p, 1);
				break;
			}
			DO_ARG('b', signed char, 1);
			DO_ARG('B', unsigned char, 1);
			DO_ARG('h', int16_t, 2);
			DO_ARG('H', uint16_t, 2);
			DO_ARG('i', int32_t, 4);
			DO_ARG('I', uint32_t, 4);
			DO_ARG('l', int64_t, 8);
			DO_ARG('L', uint64_t, 8);
		}
		repeat = 0;
	}
	return lua_gettop(L)-2;
}

static	luaL_reg misc_meth[] = {
	{ "pack",	misc_pack },
	{ "unpack",	misc_unpack },
	{ NULL }
};

int	misc_init(lua_State *L)
{
	luaL_register(L, "misc", misc_meth);
	return 0;
}

