#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H
#include "list.h"
#include "common.h"

// can't do while (0) this
#define it_foreach(it, p_itn) \
	p_itn = container_of((it)->next, struct it_node, ls); \
	l_list_foreach(p_itn, struct it_node, ls)

// linked list because we don't have time to get fancy
typedef struct it_node{
	struct l_list ls;
	unsigned long id;
	size_t base;
	size_t bound;
	int sequence;
	// for zookeeper
	char* file_path;
	char* lock_name;
    int lock_acquired;
	int lock_type;	// different types defined in zkclient.h
	pthread_mutex_t pmutex;	// only used for blocking with master lock
} it_node_t;

typedef struct it_array {
	it_node_t** array;
	size_t len;
} it_array_t;

void it_init(struct l_list* it);
void it_deinit(struct l_list* it);
int it_intersect(struct it_node* a, struct it_node* b);
struct it_node* it_insert_new(int* new, struct l_list* it, size_t base, size_t bound, unsigned long id);
struct it_node* it_insert(struct l_list* it, size_t base, size_t bound, unsigned long id);
void print_it(struct it_node* it, char* tab_buf);

#endif