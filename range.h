#ifndef RANGE_H
#define RANGE_H
#include "common.h"
#include "list.h"
#include "interval_tree.h"

struct range_file{
	struct it_head it;
	char* file_path;
	unsigned long id;
	char mode;
};

struct range{
	A_LIST_UNION(struct range_file, files, num_files, ls);
	char* name;
	unsigned long id;
};

void range_deinit(struct range* r);
int range_init(struct range* r, char* name);
struct range_file* range_add_file(struct range* r, char* file_path, unsigned long id);
struct range_file* range_add_new_file(struct range* r, char* file_path, unsigned long id);
void print_file(struct range_file* rf, char* tab_buf);
void print_range(struct range* r, char* tab_buf);

#endif