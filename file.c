#include <stdio.h>
#include <pthread.h>
#include "common.h"
#include "file.h"
#include "range.h"
#include "sql.h"

static void* finish_insert_named_range_thread(void* p_arg){
	struct range_file* rf = (struct range_file*)p_arg;
	
}

static int finish_insert_named_range_child(struct range* r){
	ret = 0;
	pthread_t* thds;
	int* is;
	void* retval;
	thds = malloc(r->num_files * sizeof(pthread_t));
	if (!thds){
		fprintf(stderr, "Malloc threads failed\n");
		goto fail;
	}
	is = malloc(r->num_files * sizeof(int));
	if (!is){
		fprintf(stderr, "Malloc is failed\n");
		goto fail;
	}
	for (i = 0; i < r->num_files; i++){
		pthread_create(&thds[i], NULL, finish_insert_named_range_thread, &r->files[i]);
	}
	// TODO
	for (i = 0; i < r->num_files; i++){
		pthread_join(thds[i], &retval);
	}
	
	goto pass;
fail:
	ret = -1;
pass:
	if (thds)
		free(thds);
	if (is)
		free(is);
	return ret;
}

void finish_insert_named_range(struct range* r){
	int i;
	int f = fork();
	if (f == 0){
		finish_insert_named_range_child(r);
	}
	else if (f > 0){
		printf("Inserting range (pid %d)\n", f);
	}
	else{
		fprintf(stderr, "Fork failed\n");
	}
}
