#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H
#include <string.h>
#include "common.h"

// can't do while (0) this
#define it_foreach(it, p_itn) \
	p_itn = container_of((it)->ls.next, struct it_node, ls); \
	l_list_foreach(p_itn, struct it_node, ls)

// line numbers are 1-indexed
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

void it_init(struct it_head* it){
	it->ls = L_LIST_NULL;
}

void it_deinit(struct it_head* it){
	struct it_node* p;
	while (it->ls.next){
		p = container_of(&it->ls.next, struct it_node, ls);
		it->ls.next = &p->ls;
		free(p);
	}
	it->ls = L_LIST_NULL;
}

int it_intersect(struct it_node* a, struct it_node* b){
	if (a->base <= b->bound && b->base <= a->bound)
		return 1;
	return 0;
}

struct it_node* it_insert(struct it_head* it, size_t base, size_t bound, unsigned long id){ // not at all thread safe
	struct it_node* p_itn, *p_f;
	struct l_list* save = &it->ls;
	struct it_node f = (struct it_node){
		.ls = L_LIST_NULL,
		.id = id,
		.base = base,
		.bound = bound,
	};
	p_f = &f;
	it_foreach(it, p_itn){
		if (it_intersect(p_f, p_itn)){
			if (p_f == &f){ // first intersection; *p_itn becomes our "base" node
				// update *p_itn to (*p_f union *p_itn), then let p_f point to our "base" node pointed to by p_itn
				if (p_itn->base > p_f->base){
					p_itn->base = p_f->base;
				}
				if (p_itn->bound < p_f->bound){
					p_itn->bound = p_f->bound;
				}
				p_f = p_itn;
			}
			else{ // not first intersection; absorb (union) *p_itn into our "base" node *p_f and free p_itn
				p_f->ls.next = &p_itn->ls;
				if (p_f->bound < p_itn->bound){
					p_f->bound = p_itn->bound; // guaranteed to fail it_intersect of next iteration
				}
				free(p_itn);
				p_itn = p_f;
			}
		}
		else{
			if (p_itn->base < p_f->base){ // save the latest node (by base) before node to insert
				save = &p_itn->ls;
			}
			else{ // beyond node to insert; break
				break;
			}
		}
	}
	if (p_f == &f){ // never intersected; insert new node
		p_f = malloc(sizeof(struct it_node));
		if (p_f != NULL){
			memcpy(p_f, &f, sizeof(struct it_node));
			l_list_add_after(save, &p_f->ls);
		}
	}
	return p_f;
}

void print_it(struct it_node* it, char* tab_buf){
	tab_out(tab_buf,
		printf("%sBase: %lu; Bound: %lu\n", tab_buf, it->base, it->bound);
	);
}

// sort array of it_nodes by the sequence number
void sort_intervals_array_by_sequence(it_node_t* intervals) {
	// TODO
}

#endif