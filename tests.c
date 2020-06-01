#include "common.h"
#ifdef COMPILE_TEST

#include "interval_tree.h"

void it_print(struct it_head* it){
	struct it_node* p_itn;
	it_foreach(it, p_itn){
		printf("id: %lu, base: %lu, bound: %lu\n", p_itn->id, p_itn->base, p_itn->bound);
	}
}

void it_test(){
	const int it_test_len = 17;
	const unsigned long bases[] = {12, 11, 200, 2, 20, 50, 50, 64, 1024, 19, 1023, 2, 301, 499, 501, 800, 750};
	const unsigned long bounds[] = {24, 24, 500, 5, 32, 55, 57, 200, 1025, 20, 1027, 5, 401, 501, 505, 900, 950};

	struct it_head it;
	it_init(&it);
	int i;
	for (i = 0; i < it_test_len; i++){
		it_insert(&it, bases[i], bounds[i], i);
	}
	it_print(&it);
}

#endif
