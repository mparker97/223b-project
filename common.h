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

#define offset_of(t, m) ((size_t)&((t*)0)->m)
#define container_of(p, t, m) ((t*)((char*)(p) - offset_of(t, m)))

extern int* opts1_m;
extern char** p_exe_path;
extern char* file_path;
extern struct range* input_range;
extern char verbosity;
extern int read_from_stdin;
extern char mode;

void err(int e){
	// TODO: frees
	exit(e);
}

void print(const char* a, const char* b, ...){
	va_list args;
	if (verbosity == 0){
		va_start(args, a);
		vfprintf(stdout, a, args);
	}
	else if (verbosity == 'v'){
		va_start(args, b);
		vfprintf(stdout, b, args);
	}
	va_end(args);
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

void* a_list_init(struct a_list* ls, size_t elm_sz){
	if (!(ls->ls = calloc(A_LIST_INIT_LEN, elm_sz))){
		return NULL;
	}
	ls->sz = 0;
	return ls;
}

void* a_list_add(struct a_list* ls, size_t elm_sz){
	if (!(ls->sz & (ls->sz - 1)) && ls->sz >= A_LIST_INIT_LEN){
		if (!(ls->ls = realloc(ls->ls, ls->sz * 2 * elm_sz))){
			return NULL;
		}
	}
	return &ls->ls[ls->sz++];
}

#endif