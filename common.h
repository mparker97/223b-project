#ifndef COMMON_H
#define COMMON_H
#include <string.h>

#define BITS_PER_BYTE 8
#define PATH_MAX 4095
#define ORACLE_LEN_MIN 8
#define ORACLE_LEN_MAX 255
#define freec(x) do{free(x); x = NULL;} while (0)
#define closec(x) do{close(x); x = 0;} while (0)

#define ID_NONE (unsigned long)(-1)
#define VERBOSITY_NORMAL 0
#define VERBOSITY_VERBOSE 1
#define VERBOSITY_QUIET 2

int* opts1_m = NULL;
char** p_exe_path = NULL;
char* file_path = NULL;
struct range* input_range;
int verbosity = VERBOSITY_NORMAL;
int read_from_stdin = 0;
char mode = 0;

void err(int e){
	// TODO: frees
	exit(e);
}

void print(const char* a, const char* b, ...){
	va_list args;
	if (verbosity == VERBOSITY_NORMAL){
		va_start(args, a);
		vprintf(stdout, a, args);
	}
	else if (verbosity == VERBOSITY_VERBOSE){
		va_start(args, b);
		vprintf(stdout, b, args);
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