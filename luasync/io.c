/*
 * $Id: io.c,v 1.3 2006-06-06 01:39:03 ezdy Exp $
 *
 * disk I/O cruft
 */


#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>

#include <dirent.h>

#include "buf.h"
#include "err.h"
#include "event.h"

#define IOFILE "lafile*"
#define DIRGC "dirgc*"

struct	iofile {
	int	fd;
};

#define tofile(L, x) luaL_checkudata(L, x, IOFILE)

static const struct {
	char *name;
	int val;
} fmodes[] = {
	{ "XOTH", S_IXOTH }, /* bit 1 */
	{ "WOTH", S_IWOTH }, /* bit 2 */
	{ "ROTH", S_IROTH }, /* bit 3 */
	{ "XGRP", S_IXGRP },
	{ "WGRP", S_IWGRP },
	{ "RGRP", S_IRGRP },
	{ "XUSR", S_IXUSR },
	{ "WUSR", S_IWUSR },
	{ "RUSR", S_IRUSR }, /* bit 9 */
	{ "ISVTX", S_ISVTX },
	{ "ISGID", S_ISGID },
	{ "ISUID", S_ISUID },
	{ "IFIFO", S_IFIFO },
	{ "IFCHR", S_IFCHR },
	{ "IFDIR", S_IFDIR },
	{ "IFBLK", S_IFBLK },
	{ "IFREG", S_IFREG },
	{ "IFLNK", S_IFLNK },
	{ "IFSOCK", S_IFSOCK }, /* 19 */
	{ NULL }
};


/* convert user-specified mode into it's real value */
static	int	str2mode(const char *s)
{
	char *fstr = strdup(s);
	char *t;
	int i, omode = 0;

	if (sscanf(s, "%u", &omode) == 1)
		return omode;
	for (t = strtok(fstr, "|,+ "); t; t = strtok(NULL, "|,+ ")) {
		for (i = 0; fmodes[i].name; i++)
			if (!strcasecmp(t, fmodes[i].name)) {
				omode |= fmodes[i].val;
				break;
			}
		if (!fmodes[i].name)
			return -1;
	}
	return omode;
}

/* convert modes into ls-like string */
static	char *mode2str(int mode)
{
	static char buf[16];
	struct {
		int mode, pos, c;
	} map[] = {
		{S_IXOTH, 9, 'x'},
		{S_IWOTH, 8, 'w'},
		{S_IROTH, 7, 'r'},
		{S_IXGRP, 6, 'x'},
		{S_IWGRP, 5, 'w'},
		{S_IRGRP, 4, 'r'},
		{S_IXUSR, 3, 'x'},
		{S_IWUSR, 2, 'w'},
		{S_IRUSR, 1, 'r'},
		{S_IFLNK, 0, '@'},
		{S_IFBLK, 0, 'b'},
		{S_IFDIR, 0, 'd'},
		{S_IFCHR, 0, 'c'},
		{S_IFIFO, 0, 'p'},
		{S_IFSOCK, 0, 's'},
		{S_ISVTX, 9, 't'},
		{S_ISUID, 3, 's'},
		{S_ISGID, 6, 's'}, {-1} };
	int	i;
	strcpy(buf, "----------");
	for (i = 0; map[i].mode != -1; i++) {
		if ((mode & map[i].mode) == map[i].mode) {
			char c = map[i].c;
			if (((c == 't') || (c == 's')) && (buf[map[i].pos] == '-'))
				c &= 0xdf;
			buf[map[i].pos] = c;
		}
	}
	return buf;
}

static	int	parse_fflags(const char *m)
{
	char c;
	int mode = 0;
	int hadr = 0;
	while ((c = *m++)) switch(c) {
		case 'r':
			hadr |= 1;
			if (hadr&2) {
				mode |= O_RDWR;
				break;
			}
			mode |= O_RDONLY;
			break;
		case 'w':
			hadr |= 2;
			if (hadr&1) {
				mode |= O_RDWR;
			}
			mode |= O_WRONLY;
			break;
		case 'c':
			mode |= O_CREAT;
			break;
		case 'x':
			mode |= O_EXCL;
			break;
	}
	return mode;
}

static	struct iofile *newfile(lua_State *L)
{
	struct iofile *f = lua_newuserdata(L, sizeof(struct iofile));
	f->fd = -1;
	luaL_getmetatable(L, IOFILE);
	lua_setmetatable(L, -2);
	return f;
}

int	io_open(lua_State *L)
{
	const char *fname = luaL_checkstring(L, 1);
	int flags = parse_fflags(luaL_optstring(L, 2, "r"));
	const char *smode = lua_tostring(L, 3);
	int fd;
	struct iofile *f;
	err_no = 0;
	if (smode) {
		int mode = str2mode(smode);
		if (mode < 0) {
			err_no = EINVAL;
			return 0;
		}
		fd = open(fname, flags, mode);
	} else {
		fd = open(fname, flags);
	}
	if (fd < 0) {
		err_no = errno;
		return 0;
	}
	f = newfile(L);
	f->fd = fd;
	return 1;
}

static	void getstat(lua_State *L, struct stat *st)
{
	int	i;
	lua_newtable(L);

#define ITEM(name,val) \
	lua_pushliteral(L, name); \
	lua_pushnumber(L, (lua_Number) st->st_##val); \
	lua_rawset(L, -3);

	ITEM("dev", dev);
	ITEM("ino", ino);
	ITEM("mode", mode);
	ITEM("nlink", nlink);
	ITEM("uid", uid);
	ITEM("gid", gid);
	ITEM("rdev", rdev);
	ITEM("atime", atime);
	ITEM("ctime", ctime);
	ITEM("mtime", mtime);
	ITEM("size", size);
	ITEM("blocks", blocks);
	ITEM("blksize", blksize);

	for (i = 0; fmodes[i].name; i++) {
		if ((st->st_mode & fmodes[i].val) == fmodes[i].val) {
			lua_pushstring(L, fmodes[i].name);
			lua_pushboolean(L, 1);
			lua_rawset(L, -3);
		}
	}

	lua_pushliteral(L, "modestr");
	lua_pushstring(L, mode2str(st->st_mode));
	lua_rawset(L, -3);

#undef ITEM
}


static	int	io_stat(lua_State *L)
{
	const char *fname = luaL_checkstring(L, 1);
	struct stat st;

	if (!stat(fname, &st)) {
		getstat(L, &st);
		return 1;
	}
	err_no = errno;
	return 0;
}

static	int	io_lstat(lua_State *L)
{
	const char *fname = luaL_checkstring(L, 1);
	struct stat st;

	if (!lstat(fname, &st)) {
		getstat(L, &st);
		return 1;
	}
	err_no = errno;
	return 0;
}

static	int	io_fstat(lua_State *L)
{
	struct iofile *f = tofile(L, 1);
	struct stat st;

	if (!fstat(f->fd, &st)) {
		getstat(L, &st);
		return 1;
	}
	err_no = errno;
	return 0;
}

#define MIN(x,y) ((x)<(y)?(x):(y))
/* read(fd, buf, count[,off]) */
static	int	io_read(lua_State *L)
{
	struct iofile *f = tofile(L, 1);
	struct luabuf *b = lua_tobuf(L, 2, BUF_HARD|BUF_CONV);
	int count = luaL_checkint(L, 3);
	int off = luaL_optint(L, 4, -1);
	int got, done = 0;
	struct	bufchain *bc;

	if (count < 0)
		return 0;

	while (count > 0) {
		int toread;

		bc = buf_grab(b, count, 0);
		toread = MIN(count, bc->raw->free);
		if (off >= 0)
			got = pread(f->fd, bc->raw->data + bc->start + bc->len, toread, off);
		else
			got = read(f->fd, bc->raw->data + bc->start + bc->len, toread);
		if (got <= 0) {
			buf_commit(b, 0);
			err_no = errno;
			return 0;
		}
		buf_commit(b, got);
		count -= got;
		done += got;
		if (off >= 0)
			off += got;
		if (got < toread)
			break;
	}
	lua_pushinteger(L, done);
	return 1;
}

static	int	io_write(lua_State *L)
{
	struct iofile *f = tofile(L, 1);
	struct luabuf *b = lua_tobuf(L, 2, BUF_CONV|BUF_HARD);
	int count = luaL_checkint(L, 3);
	int off = luaL_optint(L, 4, -1);
	int got, done = 0;
	llist *ll;

	if (count < 0)
		return 0;

	ll_for(b->chain, ll) {
		struct bufchain *bc = ll_get(ll, struct bufchain, list);
		if (off != -1) {
			got = pwrite(f->fd, bc->raw->data + bc->start, bc->len, off);
		} else {
			got = write(f->fd, bc->raw->data + bc->start, bc->len);
		}
		if (got < 0)
			break;
		done += got;
		if (off != -1)
			off += got;
		if (got < bc->len)
			break;
	}
	lua_pushinteger(L, done);
	return 1;
}

static	int	io_seek(lua_State *L)
{
	struct iofile *f = tofile(L, 1);
	off_t off = luaL_optint(L, 2, 0);
	const char *whence = luaL_optstring(L, 3, "s");
	int wh = SEEK_SET;
	switch (*whence) {
		case 's':
			wh = SEEK_SET;
			break;
		case 'e':
			wh = SEEK_END;
			break;
		case 'c':
			wh = SEEK_CUR;
			break;
	}
	off = lseek(f->fd, off, wh);
	if (off < 0) {
		err_no = errno;
		return 0;
	}
	lua_pushnumber(L, (lua_Number) off);
	return 1;
}

static	int	io_close(lua_State *L)
{
	struct iofile *f = tofile(L, 1);
	if (f->fd != -1)
		close(f->fd);
	f->fd = -1;
	return 0;
}

/* generator function */
static	int	lsdir_generator(lua_State *L)
{
	DIR **dp = lua_touserdata(L, lua_upvalueindex(1));
	struct	dirent *de;
	if (!dp)
		return 0;
	de = readdir(*dp);
	if (!de) {
		closedir(*dp);
		*dp = NULL;
		return 0;
	}
	lua_pushlstring(L, de->d_name, de->d_namlen);
	return 1;
}

/* garbage collect - close the dir handle */
static	int	lsdir_gc(lua_State *L)
{
	DIR **dp = lua_touserdata(L, 1);
	if (*dp) {
		closedir(*dp);
		*dp = NULL;
	}
	return 0;
}

/* get a generator for the directory listing */
static	int	io_lsdir(lua_State *L)
{
	DIR *dir = opendir(luaL_checkstring(L, 1));
	DIR **dp;
	err_no = 0;
	if (!dir) {
		err_no = errno;
		return 0;
	}
	dp = lua_newuserdata(L, sizeof(DIR *));
	*dp = dir;
	/* set a gc method for the directory */
	luaL_getmetatable(L, DIRGC);
	lua_setmetatable(L, -2);

	lua_pushcclosure(L, lsdir_generator, 1);
	return 1;
}

static	int	io_cd(lua_State *L)
{
	const char *nd = lua_tostring(L, 1);
	static	char cwd[1025];

	err_no = 0;
	if (getcwd(cwd, 1024))
		lua_pushstring(L, cwd);

	if (nd) {
		chdir(nd);
		err_no = errno;
		return 0;
	}
	return 1;
}

static	int	io_unlink(lua_State *L)
{
	const char *fn = luaL_checkstring(L, 1);
	err_no = 0;
	if (unlink(fn)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_rmdir(lua_State *L)
{
	const char *fn = luaL_checkstring(L, 1);
	err_no = 0;
	if (rmdir(fn)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_chmod(lua_State *L)
{
	const char *fn = luaL_checkstring(L, 1);
	const char *smode = luaL_checkstring(L, 2);
	int	mode;
	err_no = 0;
	mode = str2mode(smode);
	if (mode == -1) {
		err_no = EINVAL;
		return 0;
	}
	if (chmod(fn, mode)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_fchmod(lua_State *L)
{
	struct iofile *fn = tofile(L, 1);
	const char *smode = luaL_checkstring(L, 2);
	int	mode;
	err_no = 0;
	mode = str2mode(smode);
	if (mode == -1) {
		err_no = EINVAL;
		return 0;
	}
	if (fchmod(fn->fd, mode)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_chown(lua_State *L)
{
	const char	*fn = luaL_checkstring(L, 1);
	int	uid = luaL_optint(L, 2, -1);
	int	gid = luaL_optint(L, 3, -1);
	err_no = 0;
	if (chown(fn, uid, gid)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_lchown(lua_State *L)
{
	const char	*fn = luaL_checkstring(L, 1);
	int	uid = luaL_optint(L, 2, -1);
	int	gid = luaL_optint(L, 3, -1);
	err_no = 0;
	if (lchown(fn, uid, gid)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_fchown(lua_State *L)
{
	struct iofile *fn = tofile(L, 1);
	int	uid = luaL_optint(L, 2, -1);
	int	gid = luaL_optint(L, 3, -1);
	err_no = 0;
	if (fchown(fn->fd, uid, gid)) {
		err_no = errno;
		return 0;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static	int	io_sendfile(lua_State *L)
{
	struct iofile *from = tofile(L, 1);
	struct sock *to = tosock(L, 2);
	off_t off = lua_tointeger(L, 3);
	int count = luaL_optint(L, 4, INT_MAX);
	off_t got = 0;
	static char buf[16384];

#ifdef HAVE_SENDFILE
#ifdef HAVE_BSDSENDFILE
	/* bsd-like sendfile */
	if (sendfile(from->fd, to->fd, off, count, NULL, &got, 0))
		got = -1;
#else
	/* linux sendfile */
	got = sendfile(to->fd, from->fd, &out, count);
#endif
#else
	/* no sendfile, emulate using pread/send */
	while (count > 0) {
		int i, o, toread;

		toread = MIN(sizeof(buf), count);
		i = pread(from->fd, buf, toread, off);
		if (i < 0)
			break;
		o = send(to->fd, buf, i, 0);
		got += o;
		count -= o;

		if (i < toread || o < toread)
			break;
	}
	if (!got && count)
		got = -1;
#endif
	if (got == -1) {
		err_no = errno;
		return 0;
	}
	
	lua_pushinteger(L, got);
	return 1;
}

static	int	io_readlink(lua_State *L)
{
	static	char dst[1025];
	int got;
	got = readlink(luaL_checkstring(L, 1), dst, sizeof(dst)-1);
	err_no = 0;
	if (got < 0) {
		err_no = errno;
		return 0;
	}
	lua_pushlstring(L, dst, got);
	return 1;
}

static	luaL_reg io_meth[] = {
	{ "open",	io_open },
	{ "stat",	io_stat },
	{ "lstat",	io_lstat},
	{ "lsdir",	io_lsdir },
	{ "cd",		io_cd },
	{ "unlink",	io_unlink },
	{ "rmdir",	io_rmdir },
	{ "chmod",	io_chmod },
	{ "chown",	io_chown },
	{ "lchown",	io_lchown },
	{ "readlink",	io_readlink },
	{ NULL }
};

static	luaL_reg fd_meth[] = {
	{ "fstat",	io_fstat },
	{ "read",	io_read },
	{ "write",	io_write },
	{ "sendfile",	io_sendfile },
	{ "seek",	io_seek },
	{ "close",	io_close },
	{ "fchmod",	io_fchmod },
	{ "fchown",	io_fchown },
	{ "__gc",	io_close },
	{ NULL }
};


int	io_init(lua_State *L)
{
	luaL_newmetatable(L, IOFILE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, fd_meth);

	luaL_newmetatable(L, DIRGC);
	lua_pushcfunction(L, lsdir_gc);
	lua_setfield(L, -2, "__gc");

	luaL_register(L, "bio", io_meth);
	return 0;
}

