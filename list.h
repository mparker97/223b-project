#ifndef LIST_H
#define LIST_H
#include <stdlib.h>
#include <string.h>

struct l_list{ // linked list
	struct l_list* next;
};

#define L_LIST_NULL ((struct l_list){.next = NULL})

struct a_list{ // "amortized" list (contiguous array); initial size of A_LIST_INIT_LEN, doubles automatically when filled
	void* ls;
	int sz;
};

#define A_LIST_INIT_LEN 4
#define A_LIST_UNION(t, n0, n1, n2) \
union{ \
	struct{ \
		t* n0; \
		int n1; \
	}; \
	struct a_list n2; \
}

#include "common.h"
#define l_list_foreach(p, t, m) \
	for (; (p) != NULL; p = (((p)->m).next)? container_of(((p)->m).next, t, m) : NULL)

static inline void l_list_add_after(struct l_list* curr, struct l_list* n){
	n->next = curr->next;
	curr->next = n;
}

// delete is O(n) because I don't care

#define a_list_index(list, elm_sz, i) ((char*)((list)->ls) + (i) * (elm_sz))

static void* a_list_init(struct a_list* ls, size_t elm_sz){
	if (!ls->ls){
		if (!(ls->ls = calloc(A_LIST_INIT_LEN, elm_sz))){
			return NULL;
		}
		ls->sz = 0;
	}
	return ls->ls;
}

static void a_list_deinit(struct a_list* ls){
	if (ls->ls){
		free(ls->ls);
		ls->ls = NULL;
		ls->sz = 0;
	}
}

static void* a_list_add(struct a_list* ls, size_t elm_sz){
	if (!(ls->sz & (ls->sz - 1)) && ls->sz >= A_LIST_INIT_LEN){
		if (!(ls->ls = realloc(ls->ls, ls->sz * 2 * elm_sz))){
			return NULL;
		}
	}
	return &((char*)(ls->ls))[ls->sz++ * elm_sz];
}

static void* a_list_addc(struct a_list* ls, size_t elm_sz){
	void* r = a_list_add(ls, elm_sz);
	if (r){
		memset(r, 0, elm_sz);
	}
	return r;
}

static int a_list_delete(struct a_list* ls, size_t elm_sz, int idx){ // & shift
	int ret = -1;
	char* c;
	if (idx >= 0){
		for (c = a_list_index(ls, elm_sz, idx); c < a_list_index(ls, elm_sz, ls->sz - 1); c += elm_sz){
			memcpy(c, c + elm_sz, elm_sz);
		}
		ls->sz--;
		ret = 0;
	}
	return ret;
}

static inline void a_list_sort(struct a_list* ls, size_t elm_sz, int (*f)(const void*, const void*)){
	qsort(ls->ls, ls->sz, elm_sz, f);
}

#endif