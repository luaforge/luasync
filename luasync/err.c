/*
 * $Id: err.c,v 1.3 2006-06-06 01:39:03 ezdy Exp $
 *
 * error reporting
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


/*
Error reporting
---------------
err.no = return current error value, null on no error
err.str = return current error's string value ("Invalid argument" and alike)
err.clear() = clear current error
*/
#define __ERR_C
#include "err.h"

int	err_no = 5;
static	int	err_no_max = 0;

static	char	**err_matrix;
static	char	**err_matrix_long;

static	int	err_clear(lua_State *L)
{
	err_no = 0;
	return 0;
}

static	int	err_get(lua_State *L)
{
	const char	*s = luaL_checkstring(L, 2);
	if (!s)
		return 0;
	if (s[0] == 'n' && s[1] == 'o') {
		if (!err_no)
			return 0;
		if (err_no >= err_no_max || !err_matrix[err_no]) {
			lua_pushstring(L, "EINVAL");
			return 1;
		}

		lua_pushstring(L, err_matrix[err_no]);
		return 1;
	}


	/* long name */
	if (err_no >= err_no_max || !err_matrix_long[err_no]) {
		lua_pushfstring(L, "Unknown error %d\n", err_no);
		return 1;
	}
	lua_pushstring(L, err_matrix_long[err_no]);
	return 1;
}

luaL_Reg err_meth[] = {
	{ "clear", err_clear },
	{ NULL, NULL }
};

int err_init(lua_State *L)
{
	int	i;
	/* create error matrix */
	for (i = 0; err_names[i].name; i++) {
		if (err_names[i].val > err_no_max)
			err_no_max = err_names[i].val;
	}
	err_no_max++;
	err_matrix = malloc(err_no_max * sizeof(char *));
	err_matrix_long = malloc(err_no_max * sizeof(char *));
	for (i = 0; err_names[i].name; i++) {
		err_matrix[err_names[i].val] = err_names[i].name;
		err_matrix_long[err_names[i].val] = strerror(err_names[i].val);
	}

	luaL_register(L, "err", err_meth);

	lua_newtable(L);
	lua_pushcfunction(L, err_get);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
//	lua_pop(L, 1);

	return 0;
}

