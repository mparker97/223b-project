#include <stdio.h>
#include <unistd.h>
//#include <fcntl.h>
//#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wait.h>
#include "sql.h"
#include "common.h"
#include "range.h"
#include "help.h"
#include "zkclient.h"

#define foreach_optarg(argc, argv) for (; optind < (argc) && (argv)[optind][0] != '-'; optind++)

int main(int argc, char* argv[]){
	struct range r;
	char* name = "wow";
	char* path = "asdfasdfa./.";
	sql_init();
	range_init(&r,"whatever");
	struct range_file *rf;
	rf = range_add_file(&r,"path",1);
	range_file_add_it(rf,0,3,1);
	rf = range_add_file(&r,"path1",2);
	range_file_add_it(rf,0,4,2);
	query_insert_named_range(&r);
	struct range_file rf_ret;
	it_node_t*pp;
	//query_select_file_intervals(&rf_ret, "path", pp);
	query_select_named_range(&r);
	
}
