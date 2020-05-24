#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H
#include "common.h"

#define it_node_mask_bits 2
#define it_node_mask_on(x, m) ((x) | ((m) << (BITS_PER_BYTE * sizeof(unsigned long) - it_node_mask_bits)))
#define it_node_mask_off(itn) (((itn)->id >> (BITS_PER_BYTE * sizeof(unsigned long) - it_node_mask_bits)) & ((1UL << it_node_mask_bits) - 1))
#define IT_NODE_NORMAL 0
#define IT_NODE_LINE 1
#define IT_NODE_STRING 2

#define it_foreach_interval(it, i, itn) \
	for ()

struct it_node{
	unsigned long id;
	size_t base;
	size_t size;
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