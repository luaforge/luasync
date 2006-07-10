/*
 * $Id: sha1.h,v 1.1 2006-07-10 00:58:39 ezdy Exp $
 * sha1 cruft
 */

#ifndef __SHA1_H
#define __SHA1_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#define SHA_DIGEST_WORDS 5                                                                                             
#define SHA_WORKSPACE_WORDS 80    
#define SHA1_DIGEST_SIZE	20

struct sha1_ctx {
        u_int64_t count;
        u_int32_t state[5];
        u_int8_t buffer[64];
};


#ifndef SHA1_C
extern	int sha1_init(lua_State *L);
#endif
#endif
