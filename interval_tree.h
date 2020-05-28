#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H
#include "common.h"

#define it_foreach_interval(it, i, itn) \
	for (;;)

// line numbers are 1-indexed
struct it_node{
	unsigned long id;
	//union{
		size_t base;
	//	char* r_base;
	//};
	//union{
		size_t size;
	//	char* r_size;
	//};
};

struct it_head{
	
};

int it_init(struct it_head* it){
	
}

int it_deinit(struct it_head* it){
	
}

struct it_node* it_insert(struct it_head* it, size_t base, size_t size, unsigned long id)){
	
}

#endif