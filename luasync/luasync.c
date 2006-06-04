#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "err.h"
#include "buf.h"
#include "event.h"
#include "io.h"

LUALIB_API int luaopen_luasync(lua_State *L)
{
	err_init(L);
	buf_init(L);
	net_init(L);
	event_init(L);
	io_init(L);
	return 0;
}

