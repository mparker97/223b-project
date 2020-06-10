#ifndef COMMON_H
#define COMMON_H
//#define COMPILE_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdbool.h>
#define STR(x) #x

#define _DEBUG

// do while (0) to allow semicolon after
#ifdef _DEBUG
#define fail_check(c) \
	do { \
		if (!(c)){ \
			fprintf(stderr, "%s:%d: Assertion (" STR(c) ") Failed\n", __FILE__, __LINE__); \
			goto fail; \
		} \
	} while (0)
#else
#define fail_check(c) \
do { \
	if (!(c)){ \
		goto fail; \
	} \
} while (0)
#endif
#define WIGNORE(x, instrs) \
	do{ \
		_Pragma("GCC diagnostic push"); \
		_Pragma(STR(GCC diagnostic ignored #x)); \
		instrs; \
		_Pragma("GCC diagnostic pop"); \
	} while(0)

#define RANGE_NAME_LEN_MAX 64
#define PATH_MAX 3072
#define ORACLE_LEN_MIN 8
#define ORACLE_LEN_MAX 255
#define freec(x) do{free(x); x = NULL;} while (0)
#define closec(x) do{close(x); x = -1;} while (0)

#define ID_NONE (unsigned long)(-1)
#define BOUND_END ((size_t)(-1))

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

extern char** p_exe_path;
extern pthread_mutex_t print_lock;
#include "sql.h"
#include "range.h"

static void err(int e){
	global_rs_deinit();
	pthread_mutex_destroy(&print_lock);
	sql_end();
	// other frees
	exit(e);
}

static void err_out(bool cond, char* msg, ...){
	va_list ap;
	if (cond){
		va_start(ap, msg);
		vfprintf(stderr, msg, ap);
		va_end(ap);
		err(1);
	}
}

static char* pull_string(char* str){
	int i;
	if (str[0] == '"'){
		for (i = 1; str[i] != 0; i++){
			if (str[i] == '"' && str[i - 1] != '\\'){
				str[i] = 0;
				return str + i + 1;
			}
		}
	}
	return NULL;
}

static int p_strcmp(const void* a, const void* b){
  return strcmp(*(char* const*)a, *(char* const*)b);
}

#endif
