#ifndef COMMON_H
#define COMMON_H
//#define COMPILE_TEST
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>
#include "interval_tree.h"
#include "range.h"
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
	range_file_deinit(&global_rf);
	pthread_mutex_destroy(&print_lock);
	sql_end();
	// other frees
	exit(e);
}

void err_out(bool cond, char* msg, ...){
	va_list ap;
	if (cond){
		va_start(ap, msg);
		vfprintf(stderr, msg, ap);
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
	print_file(rf, tab_buf);
	pthread_mutex_unlock(&print_lock);
}

#endif