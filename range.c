#ifndef RANGE_H
#define RANGE_H
#include <string.h>
#include "common.h"
#include "range.h"
#include "list.h"
#include "interval_tree.h"

void range_deinit(struct range* r){
	int i;
	r->num_files = 0;
	if (r->name)
		freec(r->name);
	if (r->files){
		for (i = 0; i < r->num_files; i++){
			free(r->files[i].file_path);
			it_deinit(&r->files[i].it);
		}
	}
	a_list_deinit(&r->ls);
}

int range_init(struct range* r, char* name){
	if (!(r->name = strdup(name)))
		goto fail;
	if (!a_list_init(&r->ls, sizeof(struct range_file)))
		goto fail;
	return 0;
fail:
	fprintf(stderr, "Failed to initialize range\n");
	range_deinit(r);
	return -1;
}

struct range_file* range_add_file(struct range* r, char* file_path, unsigned long id){
	struct range_file* rf;
	char* str;
	if (strlen(file_path) > PATH_MAX)
		goto fail;
	if (!(str = strdup(file_path))){
		goto fail;
	}
	rf = a_list_add(&r->ls, sizeof(struct range_file));
	if (rf){
		rf->file_path = str;
		rf->id = id;
		return rf;
	}
fail:
	if (str)
		free(str);
	return NULL;
}

struct range_file* range_add_new_file(struct range* r, char* file_path, unsigned long id){
	int i;
	for (i = 0; i < r->num_files; i++){
		if (!strcmp(file_path, r->files[i].file_path)){
			return NULL;
		}
	}
	return range_add_file(r, file_path, id);
}

void print_file(struct range_file* rf, char* tab_buf){
	struct it_node* p_itn;
	tab_out(tab_buf,
		printf("%sFile name: %s\n", tab_buf, rf->file_path);
		it_foreach(&rf->it, p_itn){
			print_it(p_itn, tab_buf);
		}
	);
}

void print_range(struct range* r, char* tab_buf){
	int i;
	tab_out(tab_buf,
		printf("%sRange name: %s\n", tab_buf, r->name);
		for (i = 0; i < r->num_files; i++){
			print_file(&r->files[i], tab_buf);
		}
	);
}

#endif