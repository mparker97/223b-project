#include <string.h>
#include "common.h"
#include "list.h"
#include "interval_tree.h"

void it_init(struct l_list* it){
	*it = L_LIST_NULL;
}

void it_deinit(struct l_list* it){
	struct it_node* p;
	while (it->next){
		p = container_of(it->next, struct it_node, ls);
		it->next = it->next->next;
		free(p);
	}
	*it = L_LIST_NULL;
}

inline int it_intersect(struct it_node* a, struct it_node* b){
	if (a->base <= b->bound && b->base <= a->bound)
		return 1;
	return 0;
}

struct it_node* it_insert_new(int* new, struct l_list* it, size_t base, size_t bound, unsigned long id){ // not at all thread safe
	struct it_node* p_itn, *p_f;
	struct l_list* save = it;
	struct it_node f;
	memset(&f, 0, sizeof(struct it_node));
	f.ls = L_LIST_NULL;
	f.id = id;
	f.base = base;
	f.bound = bound;
	*new = 0;
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
			*new = 1;
		}
	}
	return p_f;
}

struct it_node* it_insert(struct l_list* it, size_t base, size_t bound, unsigned long id){
	int new;
	return it_insert_new(&new, it, base, bound, id);
}

void print_it(struct it_node* it, char* tab_buf){
	tab_out(tab_buf,
		printf("%sBase: %lu; Bound: %lu\n", tab_buf, it->base, it->bound);
	);
}