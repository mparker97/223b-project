#ifndef COMMON_H
#define COMMON_H
//#define COMPILE_TEST
#include <string.h>
#include <sys/types.h>

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

extern int* opts1_m;
extern char** p_exe_path;
extern struct range* input_range;

void err(int e){
	// TODO: frees
	exit(e);
}

void do_print_range(struct range* r){
	char tab_buf[8];
	tab_buf[0] = 0;
	print_range(r, tab_buf);
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

#define A_LIST_UNION(t, n0, n1, n2) \
union{ \
	struct{ \
		t* n0; \
		int n1; \
	}; \
	struct a_list n2; \
}

extern A_LIST_UNION(struct range, arr, num_ranges, ls) ranges;

void* a_list_init(struct a_list* ls, size_t elm_sz){
	if (!ls->ls){
		if (!(ls->ls = calloc(A_LIST_INIT_LEN, elm_sz))){
			return NULL;
		}
		ls->sz = 0;
	}
	return ls->ls;
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

#endif