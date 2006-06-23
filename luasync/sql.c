/*
 * $Id: sql.c,v 1.2 2006-06-23 01:38:59 ezdy Exp $
 *
 * Some basic sqlite binding
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#include <fcntl.h>
#include <unistd.h>

#include <sqlite3.h>

#include "buf.h"

static char	*errs[] = {
	"OK",
	"ERROR",
	"INTERNAL",
	"PERM",
	"ABORT",
	"BUSY",
	"LOCKED",
	"NOMEM",
	"READONLY",
	"INTERRUPT",
	"IOERR",
	"CORRUPT",
	"NOTFOUND",
	"FULL",
	"CANTOPEN",
	"PROTOCOL",
	"EMPTY",
	"SCHEMA",
	"TOOBIG",
	"CONSTRAINT",
	"MISMATCH",
	"MISUSE",
	"NOLFS",
	"AUTH",
	"FORMAT",
	"RANGE",
	"NOTADB"
};
struct	stmt {
	int	parcount;	/* parameter count for all statements. -1 if broken */
	int	needreset;	/* we're in the middle of executing query; reset needed to restart */
	int	execpos;	/* next position within sts to be executed */
	struct	sqlite3 *sql;	/* sql this belongs to */
	int	count;		/* number of statemets, 0 if completely finalized */
	sqlite3_stmt *sts[0];
};

typedef struct	db_t {
	sqlite3	*sql;
} DB;


#define DBHANDLE "db.sql*"
#define DBREGISTRY "db.registry*"
#define STHANDLE "db.stmt*"

#define checkstr(L,idx) luaL_checkstring(L,idx)
#define checkstmt(L,idx) luaL_checkudata(L,idx,STHANDLE)

#define checkdb(L,idx) luaL_checkudata(L, idx, DBHANDLE)
static	inline	DB *checkdb2(lua_State *L, int idx)
{
	DB *db = checkdb(L, idx);
	if (!db->sql)
		luaL_argerror(L, idx, "closed database handle!");
	return db;
}

static	inline	struct stmt *checkstmt2(lua_State *L, int idx)
{
	struct stmt *st = checkstmt(L, idx);
	if (!st->sql || st->parcount < 0)
		luaL_argerror(L, idx, "invalid (closed/dead) statement!");
	return st;
}

static	char *lasterr;

static	inline	char *estr(sqlite3 *db, int i)
{
	static char *unk = "internal error!";

	if (lasterr && lasterr != unk)
		free(lasterr);

	if (db) {
		lasterr = strdup(sqlite3_errmsg(db));
	} else {
		lasterr = unk;
	}
	if (i >= sizeof(errs)/sizeof(int))
		return "INTERNAL";
	return errs[i];
}

static	inline int do_finalize(sqlite3 *sql, struct stmt *stmt)
{
	int	i;
	if (!stmt->sql) return SQLITE_OK;
	for (i = 0; i < stmt->count; i++) {
		int err;
		if (!stmt->sts[i]) continue;
		err = sqlite3_finalize(stmt->sts[i]);
		if (err != SQLITE_OK) {
			stmt->parcount = -1; /* broken */
			return err;
		}
		/* finalized */
		stmt->sts[i] = NULL;
	}
	stmt->parcount = -2;
	return SQLITE_OK;
}

/* open a database */
static	int	sql_open(lua_State *L)
{
	sqlite3 *sql = NULL;
	int	err;
	DB	*db;

	err = sqlite3_open(checkstr(L, 1), &sql);
	if (!sql) {
		lua_pushnil(L);
		lua_pushstring(L, estr(NULL, err));
	}
	db = lua_newuserdata(L, sizeof(*db));
	luaL_getmetatable(L, DBHANDLE); /* db methods */
	lua_setmetatable(L, -2);

	/* register this into the registry */
	lua_pushlightuserdata(L, sql);
	lua_newtable(L);
	luaL_getmetatable(L, DBREGISTRY); /* weak keys and values */
	lua_setmetatable(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);

	db->sql = sql;
	return 1;
}

/* close a database, including all it's prepared statements */
static	int	sql_close(lua_State *L)
{
	DB	*db = checkdb(L, 1);
	int	err;

	/* closed already */
	if (!db->sql)
		return 0;

	/* now close all the statements */
	lua_pushlightuserdata(L, db->sql);
	lua_rawget(L, LUA_REGISTRYINDEX);

	lua_pushnil(L); /* first traversal key */
	while (lua_next(L, -2)) {
		struct stmt *stmt = lua_touserdata(L, -1);
		/* already finalized */
		if (!stmt->sql)
			continue;
		assert(db->sql == stmt->sql);
		if (do_finalize(db->sql, stmt) == SQLITE_OK)
			stmt->sql = NULL;
	}

	/* attempt to close */
	err = sqlite3_close(db->sql);
	if (err != SQLITE_OK) {
		/* failed, push error string */
		lua_pushstring(L, estr(db->sql, err));
		return 1;
	}
	/* succeeded, throw away our table */
	lua_pushlightuserdata(L, db->sql);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
	db->sql = NULL;
	return 0;
}

static	int	sql_err(lua_State *L)
{
	lua_pushstring(L, lasterr);
	return 1;
}

/* prepare statement(s) */
#define	MAXSTS	512
static	int	db_prepare(lua_State *L)
{
	DB	*db = checkdb2(L, 1);
	size_t	qsl;
	const	char *qs = luaL_checklstring(L, 2, &qsl);
	const	char *qse = qs + qsl;

	static	sqlite3_stmt *sts[MAXSTS];
	int	parcount = 0, i;
	struct	stmt *stmt;

	/* parse statements, one by one */
	for (i = 0; i < MAXSTS; i++) {
		int err, j;
		if (qse==qs) break;
		//printf("parsing '%s', %d\n", qs, i);
		err = sqlite3_prepare(db->sql, qs, qse-qs, &sts[i], &qs);
		if (err != SQLITE_OK) {
			char *myerr = estr(db->sql, err);
			/* delete all statements compiled so far */
			for (j = 0; j < i; j++)
				sqlite3_finalize(sts[j]);
			//db->sql->errCode = err;
			lua_pushnil(L);
			lua_pushstring(L, myerr);
			return 2;
		}
		parcount += sqlite3_bind_parameter_count(sts[i]);
	}

	/* get the db handle statement table */
	lua_pushlightuserdata(L, db->sql);
	lua_rawget(L, LUA_REGISTRYINDEX);

	/* ok, all the statements seem to be parsed successfuly, create userdata */
	stmt = lua_newuserdata(L, sizeof(*stmt) + i * sizeof(stmt->sts[0]));
	memcpy(stmt->sts, sts, i * sizeof(stmt->sts[0]));
	stmt->parcount = parcount;
	stmt->sql = db->sql;
	//printf("total count %d\n", i);
	stmt->count = i;
	stmt->execpos = 0;
	luaL_getmetatable(L, STHANDLE); /* statement methods & gc */
	lua_setmetatable(L, -2);

	/* key == value == our statement */
	lua_pushvalue(L, -1);
	lua_pushvalue(L, -1);

	/* t[k] = v */
	lua_rawset(L, -4);
	return 1;
}

static	int	st_parcount(lua_State *L)
{
	struct	stmt *st = checkstmt(L, 1);
	lua_pushinteger(L, st->parcount);
	return 1;
}

static	inline	void do_reset(struct stmt *st)
{
	int	i;
	if (!st->needreset)
		return;
	assert((st->parcount >= 0) && (st->sql));
	for (i = 0; i < st->count; i++)
		sqlite3_reset(st->sts[i]);
	st->execpos = st->needreset = 0;
}


/* bind values to statement(s) */
static	int	st_bind(lua_State *L)
{
	struct	stmt *st = checkstmt2(L, 1);
	sqlite3_stmt **sts = st->sts;
	int	top = lua_gettop(L);
	int	i, np = 0;
	const	char *s;
	size_t	sl;
	int	err = 0;

	do_reset(st);
#define BIND(name,args...) { if ((err = sqlite3_bind_##name(*sts, args)) != SQLITE_OK) break; }
	/* bind the args, one by one */
	for (i = 1; i <= st->parcount; i++) {
		//printf("i=%d np=%d\n",i,np);
		while (np >= sqlite3_bind_parameter_count(*sts)) {
			np = 0;
			sts++;
		}
		np++;
		//printf("binding arg %d\n", i+1);
		/* argument missing; pass null */
		if (i+1 > top) {
			BIND(null,np);
			continue;
		}
		/* we've an argument; figure out its value */
		if (lua_isboolean(L, i+1)) {
			BIND(int, np, lua_toboolean(L,i+1));
			continue;
		}
		if (lua_isnumber(L, i+1)) {
			lua_Number fn = lua_tonumber(L,i+1);
			//int	in = lua_tointeger(L,i+1);
			//lua_Number fin = (lua_Number) in;
			//if (fn != fin)
				BIND(double, np, fn)
			//else
			//	BIND(int64, np, in);
			continue;
		}
		s = luaL_checklstring(L, i+1, &sl);
		BIND(text, np, s, sl, SQLITE_TRANSIENT);
	}
#undef BIND
	/* binding failed .. */
	if (err != SQLITE_OK) {
		lua_pushstring(L, estr(st->sql, err));
		return 1;
	}
	return 0;
}

/* reset state */
static	int	st_reset(lua_State *L)
{
	struct	stmt *st = checkstmt2(L, 1);
	do_reset(st);
	return 0;
}

static	int	st_expired(lua_State *L)
{
	int	i;
	struct	stmt *st = checkstmt2(L, 1);
//	lua_pushboolean(L,1);
	for (i = 0; i < st->count; i++)
		if (sqlite3_expired(st->sts[i]))
			return 1;
	return 0;
}

/* execute statement(s) */
static	int	st_exec(lua_State *L)
{
	struct	stmt *st = checkstmt2(L, 1);
	sqlite3_stmt **sts = st->sts;
	int	i, err;
	int	changed = 0;

	st->needreset = 1;
	for (i = 0; i < st->count; i++) {
		while ((err = sqlite3_step(sts[i])) == SQLITE_ROW);
		if (err != SQLITE_DONE) {
			lua_pushnil(L);
			lua_pushstring(L, estr(st->sql, err));
			do_reset(st);
			return 2;
		}
		changed += sqlite3_changes(st->sql);
	}
	lua_pushinteger(L, changed);
	do_reset(st);
	return 1;
}

static	inline void	push_column(lua_State *L, struct sqlite3_stmt *row, int idx)
{
	switch (sqlite3_column_type(row, idx)) {
		case SQLITE_INTEGER:
		case SQLITE_FLOAT:
			lua_pushnumber(L, sqlite3_column_double(row, idx));
			return;
		case SQLITE_TEXT:
		case SQLITE_BLOB:
			lua_pushlstring(L, sqlite3_column_blob(row, idx), sqlite3_column_bytes(row, idx));
			return;
		case SQLITE_NULL:
			lua_pushnil(L);
			return;
		default:
			abort();
	}
}

/* execute one step and return whole row. first arg is error code, either "OK" or some sort of
   error. use sql.err to find out more. if nil, end of query */
static	int	st_row(lua_State *L)
{
	struct	stmt *st = checkstmt2(L, 1);
	sqlite3_stmt **sts = st->sts;
	int	err;
retry:
	/* dead query */
//	printf("execpos=%d %d\n", st->execpos, st->count);
	if (st->needreset && (st->execpos >= st->count))
		return 0;
	st->needreset = 1;
//	printf("execpos=%d %d\n", st->execpos, st->count);
	err = sqlite3_step(sts[st->execpos]);
	if (err == SQLITE_ROW) {
		sqlite3_stmt *cs = sts[st->execpos];
		int i, c = sqlite3_data_count(cs);
		lua_pushstring(L, errs[0]);
		for (i = 0; i < c; i++)
			push_column(L, cs, i);
		return 1 + c;
	}
	st->execpos++;
	if (err == SQLITE_DONE)
		goto retry;

	/* some sort of fatal error */
	st->execpos = st->count;
	lua_pushstring(L, estr(st->sql, err));
	return 1;
}

/* ditto, but return table on success, error string on error or nil if no results */
static	int	st_cols(lua_State *L)
{
	struct	stmt *st = checkstmt2(L, 1);
	sqlite3_stmt **sts = st->sts;
	int	err;
retry:
	/* dead query */
	if (st->needreset && (st->execpos >= st->count))
		return 0;
	st->needreset = 1;
//	printf("execpos=%d %d\n", st->execpos, st->count);
	err = sqlite3_step(sts[st->execpos]);
	if (err == SQLITE_ROW) {
		sqlite3_stmt *cs = sts[st->execpos];
		int i, c = sqlite3_data_count(cs);
		lua_newtable(L);
		for (i = 0; i < c; i++) {
			push_column(L, cs, i);
			lua_setfield(L, -2, sqlite3_column_name(cs, i));
		}
		return 1;
	}
	st->execpos++;
	if (err == SQLITE_DONE)
		goto retry;

	/* some sort of fatal error */
	st->execpos = st->count;
	lua_pushstring(L, estr(st->sql, err));
	return 1;
}


/* destroy statement(s) */
static	int	st_finalize(lua_State *L)
{
	int err;
	struct	stmt *stmt = checkstmt(L, 1);

//	fprintf(stderr, "FINALIZING %p\n", stmt);
	/* the statement has been already finalized */
	if (!stmt->sql)
		return 0;

	/* finalize the statement */
	err = do_finalize(stmt->sql, stmt);

	if (err != SQLITE_OK) {
		/* no time to finalize yet */
		lua_pushstring(L, estr(stmt->sql, err));
		return 1;
	}

	/* and remove it from the db's table */
	lua_pushlightuserdata(L, stmt->sql);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	lua_rawset(L, -3);
	stmt->sql = NULL;
	return 0;
}

static	luaL_reg	sql_meth[] = {
	{ "open",	sql_open },
	{ "close",	sql_close },
	{ "err",	sql_err },
};

static	luaL_reg	db_meth[] = {
	{ "prepare",	db_prepare },
	{ "__gc",	sql_close },
	{ NULL }
};

static	luaL_reg	st_meth[] = {
	{ "parcount",	st_parcount },
	{ "bind",	st_bind },
	{ "reset",	st_reset },
	{ "exec",	st_exec },
	{ "row",	st_row },
	{ "cols",	st_cols },
	{ "close",	st_finalize },
	{ "finalize",	st_finalize },
	{ "expired",	st_expired },
	{ "__gc",	st_finalize },
	{ NULL }
};

int	sql_init(lua_State *L)
{
	/* database/statements registry */
	luaL_newmetatable(L, DBREGISTRY);
	lua_pushliteral(L, "kv");
	lua_setfield(L, -2, "__mode");

	luaL_newmetatable(L, DBHANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, db_meth);

	luaL_newmetatable(L, STHANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, st_meth);

	luaL_register(L, "sql", sql_meth);
	return 0;
}

