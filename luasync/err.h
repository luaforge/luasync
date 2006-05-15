#ifndef __ERR_H
#define __ERR_H


struct	err_name {
	char	*name;
	int	val;
};

#ifdef __ERR_C
/* some basic errnos common to linux and freebsd.
   if 0 missing errnos as you want, the idea is to keep
   the list as exhaustive as possible, but working for most
   systems. */
struct	err_name err_names[] = {
	 { "EPERM",           EPERM },
	 { "ENOENT",          ENOENT },
	 { "ESRCH",           ESRCH },
	 { "EINTR",           EINTR },
	 { "EIO",             EIO },
	 { "ENXIO",           ENXIO },
	 { "E2BIG",           E2BIG },
	 { "ENOEXEC",         ENOEXEC },
	 { "EBADF",           EBADF },
	 { "ECHILD",          ECHILD },
	 { "EAGAIN",          EAGAIN },
	 { "ENOMEM",          ENOMEM },
	 { "EACCES",          EACCES },
	 { "EFAULT",          EFAULT },
	 { "ENOTBLK",         ENOTBLK },
	 { "EBUSY",           EBUSY },
	 { "EEXIST",          EEXIST },
	 { "EXDEV",           EXDEV },
	 { "ENODEV",          ENODEV },
	 { "ENOTDIR",         ENOTDIR },
	 { "EISDIR",          EISDIR },
	 { "EINVAL",          EINVAL },
	 { "ENFILE",          ENFILE },
	 { "EMFILE",          EMFILE },
	 { "ENOTTY",          ENOTTY },
	 { "ETXTBSY",         ETXTBSY },
	 { "EFBIG",           EFBIG },
	 { "ENOSPC",          ENOSPC },
	 { "ESPIPE",          ESPIPE },
	 { "EROFS",           EROFS },
	 { "EMLINK",          EMLINK },
	 { "EPIPE",           EPIPE },
	 { "EDOM",            EDOM },
	 { "ERANGE",          ERANGE },
	 { "EDEADLK",         EDEADLK },
	 { "ENAMETOOLONG",    ENAMETOOLONG },
	 { "ENOLCK",          ENOLCK },
	 { "ENOSYS",          ENOSYS },
	 { "ENOTEMPTY",       ENOTEMPTY },
	 { "ELOOP",           ELOOP },
	 { "EWOULDBLOCK",     EWOULDBLOCK },
	 { "ENOMSG",          ENOMSG },
	 { "EIDRM",           EIDRM },
	 { "ENOSTR",          ENOSTR },
	 { "ENODATA",         ENODATA },
	 { "ETIME",           ETIME },
	 { "ENOSR",           ENOSR },
	 { "EREMOTE",         EREMOTE },
	 { "ENOLINK",         ENOLINK },
	 { "EPROTO",          EPROTO },
	 { "EMULTIHOP",       EMULTIHOP },
	 { "EBADMSG",         EBADMSG },
	 { "EOVERFLOW",       EOVERFLOW },
	 { "EILSEQ",          EILSEQ },
	 { "EUSERS",          EUSERS },
	 { "ENOTSOCK",        ENOTSOCK },
	 { "EDESTADDRREQ",    EDESTADDRREQ },
	 { "EMSGSIZE",        EMSGSIZE },
	 { "EPROTOTYPE",      EPROTOTYPE },
	 { "ENOPROTOOPT",     ENOPROTOOPT },
	 { "EPROTONOSUPPORT", EPROTONOSUPPORT },
	 { "ESOCKTNOSUPPORT", ESOCKTNOSUPPORT },
	 { "EOPNOTSUPP",      EOPNOTSUPP },
	 { "EPFNOSUPPORT",    EPFNOSUPPORT },
	 { "EAFNOSUPPORT",    EAFNOSUPPORT },
	 { "EADDRINUSE",      EADDRINUSE },
	 { "EADDRNOTAVAIL",   EADDRNOTAVAIL },
	 { "ENETDOWN",        ENETDOWN },
	 { "ENETUNREACH",     ENETUNREACH },
	 { "ENETRESET",       ENETRESET },
	 { "ECONNABORTED",    ECONNABORTED },
	 { "ECONNRESET",      ECONNRESET },
	 { "ENOBUFS",         ENOBUFS },
	 { "EISCONN",         EISCONN },
	 { "ENOTCONN",        ENOTCONN },
	 { "ESHUTDOWN",       ESHUTDOWN },
	 { "ETOOMANYREFS",    ETOOMANYREFS },
	 { "ETIMEDOUT",       ETIMEDOUT },
	 { "ECONNREFUSED",    ECONNREFUSED },
	 { "EHOSTDOWN",       EHOSTDOWN },
	 { "EHOSTUNREACH",    EHOSTUNREACH },
	 { "EALREADY",        EALREADY },
	 { "EINPROGRESS",     EINPROGRESS },
	 { "ESTALE",          ESTALE },
	 { "EDQUOT",          EDQUOT },
	 { "ECANCELED",       ECANCELED },
	 { NULL,              0 }
};
#else
extern int err_no;
extern int err_init(lua_State *L);
#endif
#endif
