#ifndef __LL_H
#define __LL_H
#define LL_INIT(x) \
	{ &x, &x }

#define LL_CLEAR(x) \
	 do { (x).prev = &(x); (x).next = &(x); } while (0);

typedef struct	llist_s {
	struct	llist_s *prev;
	struct	llist_s *next;
} llist;

static	inline int ll_empty(llist *l)
{
	return l->next == l;
}

static	inline void ll_init(llist *to)
{
	to->prev = to->next = to;
}

static	inline void ll_add(llist *to, llist *what)
{
	what->next = to->next;
	what->prev = to;
	to->next->prev = what;
	to->next = what;
}

static	inline void ll_del(llist *what)
{
	what->next->prev = what->prev;
	what->prev->next = what->next;
}


static	inline void ll_cutprev(llist *list, llist *what)
{
	what->prev = list;
	list->next = what;
}

static	inline	void ll_cutnext(llist *list, llist *what)
{
	what->next = list;
	list->prev = what;
}

static	inline void ll_addlist(llist *to, llist *what)
{
	if (!ll_empty(what)) {
		what->prev->next = to->next;
		what->next->prev = to;
		to->next->prev = what->prev;
		to->next = what->next;
	}
}

#define ll_get(l,type,lname) \
	((type *) (((char *) l) - ((char *) &(((type *) 0)->lname))))

#define ll_for(l, iter) \
	for (iter = (l).next; iter != &(l); iter = iter->next)

#define ll_forsafe(l, iter, _next) \
	for (iter = (l).next, _next = iter->next; iter != &(l); iter = _next, _next=iter->next)
#endif

