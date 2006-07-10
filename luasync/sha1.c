/*
 * $Id: sha1.c,v 1.1 2006-07-10 00:58:39 ezdy Exp $
 * sha1 cruft. stolen from linux kernel
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include <lualib.h>
#include <lauxlib.h>

#define SHA1_C
#include "sha1.h"
#include "buf.h"

#define SHA1CTX "sha1*"

static inline u_int32_t rol32(u_int32_t word, unsigned int shift)                                                              
{                                                                                                                      
        return (word << shift) | (word >> (32 - shift));                                                               
}  


/* The SHA f()-functions.  */

#define f1(x,y,z)   (z ^ (x & (y ^ z)))		/* x ? y : z */
#define f2(x,y,z)   (x ^ y ^ z)			/* XOR */
#define f3(x,y,z)   ((x & y) + (z & (x ^ y)))	/* majority */

/* The SHA Mysterious Constants */

#define K1  0x5A827999L			/* Rounds  0-19: sqrt(2) * 2^30 */
#define K2  0x6ED9EBA1L			/* Rounds 20-39: sqrt(3) * 2^30 */
#define K3  0x8F1BBCDCL			/* Rounds 40-59: sqrt(5) * 2^30 */
#define K4  0xCA62C1D6L			/* Rounds 60-79: sqrt(10) * 2^30 */

/*
 * sha_transform: single block SHA1 transform
 *
 * @digest: 160 bit digest to update
 * @data:   512 bits of data to hash
 * @W:      80 words of workspace (see note)
 *
 * This function generates a SHA1 digest for a single 512-bit block.
 * Be warned, it does not handle padding and message digest, do not
 * confuse it with the full FIPS 180-1 digest algorithm for variable
 * length messages.
 *
 * Note: If the hash is security sensitive, the caller should be sure
 * to clear the workspace. This is left to the caller to avoid
 * unnecessary clears between chained hashing operations.
 */
static void sha_transform(u_int32_t *digest, const char *in, u_int32_t *W)
{
	u_int32_t a, b, c, d, e, t, i;

	for (i = 0; i < 16; i++)
		W[i] = ntohl(((const u_int32_t *)in)[i]);

	for (i = 0; i < 64; i++)
		W[i+16] = rol32(W[i+13] ^ W[i+8] ^ W[i+2] ^ W[i], 1);

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];

	for (i = 0; i < 20; i++) {
		t = f1(b, c, d) + K1 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 40; i ++) {
		t = f2(b, c, d) + K2 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 60; i ++) {
		t = f3(b, c, d) + K3 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 80; i ++) {
		t = f2(b, c, d) + K4 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
	digest[4] += e;
}

static	void sha1_start(void *ctx)
{
	struct sha1_ctx *sctx = ctx;
	static const struct sha1_ctx initstate = {
	  0,
	  { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 },
	  { 0, }
	};

	*sctx = initstate;
}

static	void sha1_update(void *ctx, const u_int8_t *data, unsigned int len)
{
	struct sha1_ctx *sctx = ctx;
	unsigned int i, j;
	u_int32_t temp[SHA_WORKSPACE_WORDS];

	j = (sctx->count >> 3) & 0x3f;
	sctx->count += len << 3;

	if ((j + len) > 63) {
		memcpy(&sctx->buffer[j], data, (i = 64-j));
		sha_transform(sctx->state, sctx->buffer, temp);
		for ( ; i + 63 < len; i += 64) {
			sha_transform(sctx->state, &data[i], temp);
		}
		j = 0;
	}
	else i = 0;
	memset(temp, 0, sizeof(temp));
	memcpy(&sctx->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */
static	void sha1_final(void* ctx, u_int8_t *out)
{
	struct sha1_ctx *sctx = ctx;
	u_int32_t i, j, index, padlen;
	u_int64_t t;
	u_int8_t bits[8] = { 0, };
	static const u_int8_t padding[64] = { 0x80, };

	t = sctx->count;
	bits[7] = 0xff & t; t>>=8;
	bits[6] = 0xff & t; t>>=8;
	bits[5] = 0xff & t; t>>=8;
	bits[4] = 0xff & t; t>>=8;
	bits[3] = 0xff & t; t>>=8;
	bits[2] = 0xff & t; t>>=8;
	bits[1] = 0xff & t; t>>=8;
	bits[0] = 0xff & t;

	/* Pad out to 56 mod 64 */
	index = (sctx->count >> 3) & 0x3f;
	padlen = (index < 56) ? (56 - index) : ((64+56) - index);
	sha1_update(sctx, padding, padlen);

	/* Append length */
	sha1_update(sctx, bits, sizeof bits); 

	/* Store state in digest */
	for (i = j = 0; i < 5; i++, j += 4) {
		u_int32_t t2 = sctx->state[i];
		out[j+3] = t2 & 0xff; t2>>=8;
		out[j+2] = t2 & 0xff; t2>>=8;
		out[j+1] = t2 & 0xff; t2>>=8;
		out[j  ] = t2 & 0xff;
	}

	/* Wipe context */
	memset(sctx, 0, sizeof *sctx);
}

static	int sha1_new(lua_State *L)
{
	struct sha1_ctx *ctx = lua_newuserdata(L, sizeof(*ctx));
	sha1_start(ctx);
	luaL_getmetatable(L, SHA1CTX);
	lua_setmetatable(L, -2);
	return 1;
}

static	int sha1_hash(lua_State *L)
{
	struct	luabuf *in = lua_tobuf(L, 1, BUF_HARD|BUF_CONV);
	struct	sha1_ctx ctx;
	llist	*i;
	sha1_start(&ctx);
	u_int8_t out[SHA1_DIGEST_SIZE];

	ll_for(in->chain, i) {
		struct bufchain *bc = ll_get(i, struct bufchain, list);
		sha1_update(&ctx, bc->raw->data + bc->start, bc->len);
	}
	sha1_final(&ctx, out);
	buf_fromstring(L, out, sizeof(out), 0);
	return 1;
}

static	int ctx_update(lua_State *L)
{
	struct	sha1_ctx *ctx = luaL_checkudata(L, 1, SHA1CTX);
	struct	luabuf *in = lua_tobuf(L, 2, BUF_HARD|BUF_CONV);
	llist *i;
	ll_for(in->chain, i) {
		struct bufchain *bc = ll_get(i, struct bufchain, list);
		sha1_update(ctx, bc->raw->data + bc->start, bc->len);
	}
	return 0;
}

static	int ctx_final(lua_State *L)
{
	u_int8_t out[SHA1_DIGEST_SIZE];
	struct	sha1_ctx *ctx = luaL_checkudata(L, 1, SHA1CTX);
	sha1_final(&ctx, out);
	buf_fromstring(L, out, sizeof(out), 0);
	return 1;
}

static	luaL_reg ctx_meth[] = {
	{ "update",	ctx_update },
	{ "final",	ctx_final },
	{ NULL }
};

static	luaL_reg sha1_meth[] = {
	{ "new",	sha1_new },
	{ "hash",	sha1_hash },
	{ NULL }
};

int	sha1_init(lua_State *L)
{
	luaL_newmetatable(L, SHA1CTX);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, ctx_meth);

	luaL_register(L, "sha1", sha1_meth);
	return 0;
}

