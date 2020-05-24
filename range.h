#ifndef RANGE_H
#define RANGE_H
#include "common.h"
#include "interval_tree.h"

#define ID_NONE (unsigned long)(-1)

struct range_file{
	struct it_head it;
	char* file_path;
	unsigned long id;
};

struct range{
	union{
		struct{
			struct range_file* files;
			int num_files;
		};
		struct a_list ls;
	};
	char* name;
	unsigned long id;
};

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
		freec(r->files);
	}
}

int range_add_file(struct range* r, char* file_path, unsigned long id){
	struct range_file* rf;
	char* str;
	if (!(str = strdup(file_path))){
		goto fail;
	}
	rf = a_list_add(&r->ls, sizeof(struct range_file));
	if (rf){
		rf->name = str;
		rf->id = id;
		return r->num_files - 1;
	}
fail:
	if (str)
		free(str);
	return -1;
}

int range_add_new_file(struct range* r, char* file_path, unsigned long id){
	int i;
	for (i = 0; i < r->num_files; i++){
		if (!strcmp(file_path, r->files[i].file_path)){
			return i;
		}
	}
	return range_add_file(r, file_path, id);
}

#endif