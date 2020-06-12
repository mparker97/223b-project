#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "../file.h"
#include "../sql.h"
#include "../common.h"
#include "../interval_tree.h"
#include "../range.h"

//static struct range my_range;

// Range00 -f fs/file0.c -r 21,55 354,439 -f fs/file1.c -r 40,142 -f fs/journal/Day0.txt -r 37,174

static int make_range(struct range* r){
	struct range_file* rf;
	
	rf = range_add_new_file(r, "/home/ubuntu/223b-project/nosql/fs/file0.c", ID_NONE);
	fail_check(rf);
	fail_check(range_file_add_it(rf, 21, 55, ID_NONE));
	fail_check(range_file_add_it(rf, 354, 439, ID_NONE));
	
	rf = range_add_new_file(r, "/home/ubuntu/223b-project/nosql/fs/file1.c", ID_NONE);
	fail_check(rf);
	fail_check(range_file_add_it(rf, 40, 142, ID_NONE));
	
	rf = range_add_new_file(r, "/home/ubuntu/223b-project/nosql/fs/journal/Day0.txt", ID_NONE);
	fail_check(rf);
	fail_check(range_file_add_it(rf, 37, 174, ID_NONE));
	
	return 0;
fail:
	return -1;
}

MYSQL global_mysql;

int sql_init(MYSQL* mysql){return 0;}
void sql_deinit(MYSQL* mysql, int is_thread){}

int query_select_named_range(MYSQL* sql, struct range* r, char** files, int lock){ // range already has r->name
	int ret = 0;
	fail_check(make_range(r) >= 0);
	printf("Range constructed successfully\n");
	
	fail_check(prepare_file_threads(r) >= 0);
	
	goto pass;
fail:
	ret = -1;
pass:
	return ret;
}

int query_select_file_intervals(MYSQL* sql, struct range_file* rf, char* file_path, unsigned long cur_id){
	int ret = 0;
	do_print_file(rf);
	goto pass;
//fail:
	ret = -1;
pass:
	return ret;
}

int query_insert_named_range(MYSQL* sql, struct range* r){
	int ret = 0;
	do_print_range(r);
	goto pass;
//fail:
	ret = -1;
pass:
	return ret;
}

int query_resize_file(MYSQL* sql, struct range_file* rf, struct oracles* o, int swp_fd){
	int ret = 0, i = 0, j;
	struct it_node* r_itn;
	struct it_node* p_itn;
	struct l_list* l;
	struct offset_update* ou = NULL;
	struct range r;
	range_init(&r, "DUMMYRANGE");
	fail_check(make_range(&r) >= 0);
	printf("Range constructed successfully\n");
	ou = malloc(rf->num_it * sizeof(struct offset_update));
	fail_check(ou);
	
	printf("\nsearching for %s\n", r.files[i].file_path);
	for (j = 0; j < r.num_files; j++){
		if (!strcmp(rf->file_path, r.files[j].file_path)){
			printf("\tfound in %s\n", rf->file_path);
			break;
		}
		printf("\tnot found in %s\n", rf->file_path);
	}
	fail_check(j < r.num_files);
	l = &r.files[j].it;

	it_foreach(&rf->it, p_itn){
		r_itn = container_of(l->next, struct it_node, ls);
		ou[i].backing_start = r_itn->base;
		ou[i].backing_end = r_itn->bound;
		ou[i].swp_start = p_itn->base;
		ou[i].swp_end = p_itn->bound;
		l = l->next;
		i++;
	}
	fail_check(write_offset_update(rf, ou, o, swp_fd) >= 0);
	
	goto pass;
fail:
	ret = -1;
pass:
	range_deinit(&r);
	if (ou)
		free(ou);
	return ret;
}
