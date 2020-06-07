#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../file.h"
#include "../sql.h"
#include "../common.h"
#include "../interval_tree.h"
#include "../range.h"

// do while (0) to allow semicolon after
#define fail_check(c) \
	do { \
		if (!(c)){ \
			goto fail; \
		} \
	} while (0)

int make_my_range(struct range* r, char* name){
	
}

int query_select_named_range(struct range* r){ // range already has r->name
	
	goto pass;
fail:
	ret = -1;
pass:
	return ret;
}

int query_select_file_intervals(struct range_file* rf, char* file_path, unsigned long cur_id){
	
	do_print_file(rf);
	goto pass;
fail:
	ret = -1;
pass:
	return ret;
}

int query_insert_named_range(struct range* r){
	do_print_range(r);
	goto pass;
fail:
	ret = -1;
pass:
	return ret;
}

int query_resize_file(struct range_file* rf, int swp_fd, int backing_fd, struct oracles* o){
	struct it_node* p_itn;
	int i;
	ou = malloc(rf->num_it * sizeof(struct offset_update));
	fail_check(ou);

	it_foreach(&rf->it, p_itn){
		memcpy(&itn, p_itn, sizeof(struct it_node));
		ou[i].backing_start = db_base;
		ou[i].backing_end = db_bound;
		ou[i].swp_start = itn.base;
		ou[i].swp_end = itn.bound;
		i++;
	}
	fail_check(write_offset_update(ou, rf->num_it, swp_fd, backing_fd, &o) > 0);
	
	goto pass;
fail:
	ret = -1;
pass:
	// release failed earlier
	if (ou)
		free(ou);
	return ret;
}
