#ifndef __NET_H
#define __NET_H

#define DEFAULT_LISTEN_QUEUE 16
#define SOCKHANDLE "sock*"

struct	sock {
	int	fd;	/* network socket descriptor */
	int	flags;
};

extern	int	net_init(struct lua_State *L);

/* linux specific */
#ifndef UDP_CORK
#define UDP_CORK -1
#endif
#ifndef TCP_CORK
#define TCP_CORK -1
#endif
#ifndef TCP_DEFER_ACCEPT
#define TCP_DEFER_ACCEPT -1
#endif

#endif

