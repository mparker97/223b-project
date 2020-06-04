#ifndef COMMON_H
#define COMMON_H
//#define COMPILE_TEST
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>
#include "sql.h"

#define BITS_PER_BYTE 8
#define PATH_MAX 4095
#define ORACLE_LEN_MIN 8
#define ORACLE_LEN_MAX 255
#define freec(x) do{free(x); x = NULL;} while (0)
#define closec(x) do{close(x); x = 0;} while (0)

#define ID_NONE (unsigned long)(-1)
#define INVALID_SIZE_T (size_t)(-1)

#define offset_of(t, m) ((size_t)&((t*)0)->m)
#define container_of(p, t, m) ((t*)((char*)(p) - offset_of(t, m)))

#define tab_out(buf, instrs) \
	do { \
		const size_t TAB_OUT_BUF_LEN = strlen(buf); \
		(buf)[TAB_OUT_BUF_LEN + 1] = 0; \
		(buf)[TAB_OUT_BUF_LEN] = '\t'; \
		instrs; \
		(buf)[TAB_OUT_BUF_LEN] = 0; \
	} while (0)

extern struct range global_r;
//extern A_LIST_UNION(char*, arr, num_files, ls) files;
extern struct range_file global_rf;
extern pthread_mutex_t print_lock;

void err(int e){
	range_deinit(&global_r);
	it_deinit(&global_rf.it);
	pthread_mutex_destroy(&print_lock);
	sql_end();
	// other frees
	exit(e);
}

void err_out(bool cond, char* msg, ...){
	va_list ap;
	if (cond){
		va_start(ap, msg);
		vprintf(stderr, msg, ap);
		va_end(ap);
		err(1);
	}
}

void do_print_range(struct range* r){
	char tab_buf[8];
	tab_buf[0] = 0;
	pthread_mutex_lock(&print_lock);
	print_range(r, tab_buf);
	pthread_mutex_unlock(&print_lock);
}

void do_print_file(struct range_file* rf){
	char tab_buf[8];
	tab_buf[0] = 0;
	pthread_mutex_lock(&print_lock);
	print_file(r, tab_buf);
	pthread_mutex_unlock(&print_lock);
}

ssize_t substrn(const char* str, size_t str_len, char* src, size_t src_len){ // find a leading portion of str (nonzero length str_len) in src (length src_len); return index of start
	size_t i, j;
	for (i = j = 0; i < src_len; i++){
		if (j == str_len){
			break;
		}
		if (src[i] == str[j]){
			j++;
		}
		else {
			j = 0;
		}
	}
	if (!j)
		return -1;
	else
		return i - j;
}

size_t string_char_count(char* s, char c){
	size_t ret = 0;
	for (s = strchr(s, c); s != NULL; s = strchr(s + 1, c), ret++);
	return ret;
}

struct l_list{ // linked list
	struct l_list* next;
};

#define L_LIST_NULL ((struct l_list){.next = NULL})
#define l_list_foreach(p, t, m) \
	for (; (p) != NULL; p = (((p)->m).next)? container_of(((p)->m).next, t, m) : NULL)

static inline void l_list_add_after(struct l_list* curr, struct l_list* n){
	n->next = curr->next;
	curr->next = n;
}

#define A_LIST_INIT_LEN 4
struct a_list{ // "amortized" list (contiguous array); initial size of A_LIST_INIT_LEN, doubles automatically when filled
	void* ls;
	int sz;
};
// delete is O(n) because I don't care

#define A_LIST_UNION(t, n0, n1, n2) \
union{ \
	struct{ \
		t* n0; \
		int n1; \
	}; \
	struct a_list n2; \
}

#define a_list_index(list, elm_sz, i) ((char*)((list)->ls) + (i) * (elm_sz))

void* a_list_init(struct a_list* ls, size_t elm_sz){
	if (!ls->ls){
		if (!(ls->ls = calloc(A_LIST_INIT_LEN, elm_sz))){
			return NULL;
		}
		ls->sz = 0;
	}
	return ls->ls;
}

void a_list_deinit(struct a_list* ls){
	if (ls->ls){
		freec(ls->ls);
		ls->sz = 0;
	}
}

void* a_list_add(struct a_list* ls, size_t elm_sz){
	if (!(ls->sz & (ls->sz - 1)) && ls->sz >= A_LIST_INIT_LEN){
		if (!(ls->ls = realloc(ls->ls, ls->sz * 2 * elm_sz))){
			return NULL;
		}
	}
	return &ls->ls[ls->sz++];
}

void* a_list_addc(struct a_list* ls, size_t elm_sz){
	void* r = a_list_add(ls, elm_sz);
	if (r){
		memset(r, 0, elm_sz);
	}
	return r;
}

int a_list_delete(struct a_list* ls, size_t elm_sz, int idx){ // & shift
	int ret = -1;
	char* c;
	if (idx >= 0){
		for (c = a_list_index(ls, elm_sz, idx); c < a_list_index(ls, elm_sz, ls->sz - 1); c += elm_sz){
			memcpy(c, c + elm_sz, elm_sz);
		}
		ls->sz--;
	}
	return &ls->ls[ls->sz++];
}

void a_list_sort(struct a_list* ls, elm_sz, int (*f)(void* void*)){
	qsort(ls->ls, ls->sz, elm_sz, f);
}

#endif