#ifndef RANGE_H
#define RANGE_H
#include <pthread.h>
static inline void global_rs_deinit();
#include "list.h"
#include "interval_tree.h"

struct range_file{
	struct l_list it;
	char* file_path;
	unsigned long id;
	int num_it;
};

struct range{
	A_LIST_UNION(struct range_file, files, num_files, ls);
	char* name;
	unsigned long id;
};

extern struct range global_r;
extern struct range_file global_rf;

int range_init(struct range* r, char* name);
void range_deinit(struct range* r);
void range_file_deinit(struct range_file* rf);
struct range_file* range_add_file(struct range* r, char* file_path, unsigned long id);
struct range_file* range_add_new_file(struct range* r, char* file_path, unsigned long id);
struct it_node* range_file_add_it(struct range_file* rf, size_t base, size_t bound, unsigned long id);
void print_file(struct range_file* rf, char* tab_buf);
void print_range(struct range* r, char* tab_buf);

static void do_print_range(struct range* r){
	char tab_buf[8];
	tab_buf[0] = 0;
	pthread_mutex_lock(&print_lock);
	print_range(r, tab_buf);
	pthread_mutex_unlock(&print_lock);
}

static void do_print_file(struct range_file* rf){
	char tab_buf[8];
	tab_buf[0] = 0;
	pthread_mutex_lock(&print_lock);
	print_file(rf, tab_buf);
	pthread_mutex_unlock(&print_lock);
}

static inline void global_rs_deinit(){
	range_deinit(&global_r);
	range_file_deinit(&global_rf);
}

#endif