#ifndef COMMON_H
#define COMMON_H
//#define COMPILE_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
//#include <sys/types.h>
#include <stdbool.h>
#include "sql.h"

#define BITS_PER_BYTE 8
#define PATH_MAX 4095
#define ORACLE_LEN_MIN 8
#define ORACLE_LEN_MAX 255
#define freec(x) do{free(x); x = NULL;} while (0)
#define closec(x) do{close(x); x = 0;} while (0)

#define ID_NONE (unsigned long)(-1)

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

extern pthread_mutex_t print_lock;
void global_rs_deinit();

void err(int e){
	global_rs_deinit();
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

#endif