#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H
#include "common.h"
#include "list.h"

// can't do while (0) this
#define it_foreach(it, p_itn) \
	p_itn = container_of((it)->ls.next, struct it_node, ls); \
	l_list_foreach(p_itn, struct it_node, ls)

typedef struct it_node{
	struct l_list ls;
	unsigned long id;
	size_t base;
	size_t bound;
	int sequence;
} it_node_t;

typedef struct it_array {
	it_node_t** array;
	size_t len;
} it_array_t;

// linked list because we don't have time to get fancy
struct it_head{
	struct l_list ls;
};

void it_init(struct it_head* it);
void it_deinit(struct it_head* it);
int it_intersect(struct it_node* a, struct it_node* b);
struct it_node* it_insert(struct it_head* it, size_t base, size_t bound, unsigned long id);
void print_it(struct it_node* it, char* tab_buf);

// sort array of it_nodes by the sequence number
void sort_intervals_array_by_sequence(it_node_t* intervals) {
	// TODO
}

#endif