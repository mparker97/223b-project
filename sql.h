#ifndef SQL_H
#define SQL_H
#include "range.h"

struct range* query_select_named_range(char* name);
int query_insert_named_range(struct range* r, char* user);
int query_resize_file(struct range_file* f);

#endif