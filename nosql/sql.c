#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../file.h"
#include "../sql.h"
#include "../common.h"
#include "../interval_tree.h"
#include "../range.h"

/*
static struct range my_range;

int make_my_range(char* name){
	struct range_file* rf;
	fail_check(range_init(&my_range, name) >= 0);
	
	rf = range_add_new_file(&my_range, "/home/ubuntu/223b-project/nosql/fs/file0.c");
	fail_check(rf);
	fail_check(range_file_add_it(rf, 21, , ID_NONE));
	fail_check(range_file_add_it(rf, , , ID_NONE));
	fail_check(range_file_add_it(rf, , , ID_NONE));
	
	rf = range_add_new_file(&my_range, "/home/ubuntu/223b-project/nosql/fs/file1.c");
	fail_check(rf);
	fail_check(range_file_add_it(rf, 0, 100, ID_NONE));
	
	rf = range_add_new_file(&my_range, "/home/ubuntu/223b-project/nosql/fs/file2.c");
	fail_check(rf);
	fail_check(range_file_add_it(rf, 15, 45, ID_NONE));
	fail_check(range_file_add_it(rf, 25, 35, ID_NONE));
	
	return 0;
fail:
	return -1;
}*/

int sql_init(){return 0;}
void sql_end(){}

int query_select_named_range(struct range* r){ // range already has r->name
	int ret = 0;
	
	goto pass;
//fail:
	ret = -1;
pass:
	return ret;
}

int query_select_file_intervals(struct range_file* rf, char* file_path, unsigned long cur_id){
	int ret = 0;
	do_print_file(rf);
	goto pass;
//fail:
	ret = -1;
pass:
	return ret;
}

int query_insert_named_range(struct range* r){
	int ret = 0;
	do_print_range(r);
	goto pass;
//fail:
	ret = -1;
pass:
	return ret;
}

int query_resize_file(struct range_file* rf, int swp_fd, int backing_fd, struct oracles* o){
	int ret = 0, i;
	struct it_node itn;
	struct it_node* p_itn;
	struct offset_update* ou;
	ou = malloc(rf->num_it * sizeof(struct offset_update));
	fail_check(ou);

	it_foreach(&rf->it, p_itn){
		memcpy(&itn, p_itn, sizeof(struct it_node));
		/*
		ou[i].backing_start = db_base;
		ou[i].backing_end = db_bound;
		ou[i].swp_start = itn.base;
		ou[i].swp_end = itn.bound;
		*/
		i++;
	}
	fail_check(write_offset_update(ou, rf->num_it, swp_fd, backing_fd, o) > 0);
	
	goto pass;
fail:
	ret = -1;
pass:
	// release failed earlier
	if (ou)
		free(ou);
	return ret;
}
