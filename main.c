#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "options.h"
#include "file.h"
#include "sql.h"
#include "common.h"
#include "range.h"
#include "help.h"
#include "zkclient.h"

#define foreach_optarg(argc, argv) for (; optind < (argc) && (argv)[optind][0] != '-'; optind++)

extern char swp_dir[PATH_MAX + 1];
int multiple_mode;
struct range global_r;
struct range_file global_rf;
pthread_mutex_t print_lock;
char** p_exe_path;
int exe_argc;

void get_range(size_t* base, size_t* bound, char* str){
	*base = atol(str);
	if ((str = strchr(str, ','))){
		if (str[1]){
			*bound = atol(str + 1);
			err_out(*bound <= *base, "Invalid region: base %lu, bound %lu\n", *base, *bound);
			return;
		}
		*bound = BOUND_END;
	}
	fprintf(stderr, "Malformed region: \"%s\"\n", str);
	err(1);
}

void* thd_prange(void* arg){
	struct range r;
	char* name = (char*)arg;
	char* null_p = NULL;
	if (range_init(&r, name) >= 0){
		if (query_select_named_range(&r, &null_p, 0) >= 0){
			do_print_range(&r);
		}
		else{
			fprintf(stderr, "Unable to print range %s\n", name);
		}
		range_deinit(&r);
	}
	return NULL;
}

void* thd_pfile(void* arg){
	struct range_file rf;
	char* name = (char*)arg;
	it_init(&rf.it);
	if (query_select_file_intervals(&rf, name, ID_NONE) >= 0){
		do_print_file(&rf);
	}
	else{
		fprintf(stderr, "Unable to print file %s\n", name);
	}
	range_file_deinit(&rf);
	return NULL;
}

void opts(int argc, char* argv[]){
	struct range_file* rf = NULL;
	size_t base, bound;
	struct range_file** fs = NULL;
	char* buf = "+f:\0+r:";
	pthread_t* thds = NULL;
	int i = 0, j, k, l;
	char c = getopt(argc, argv, "+g:hn:pr:w:");
	if (c == 'h'){ // [h]elp
		print_help(argv[0]);
		err(0);
	}
	// if not help, do init
	err_out(!getcwd(swp_dir, PATH_MAX)
		|| pthread_mutex_init(&print_lock, NULL)
		|| options_file_init() < 0
		|| sql_init() < 0
		|| zkclient_init() < 0,
		"Failed to initialize\n");
	it_init(&global_rf.it);
	
	switch (c){
		case 'r': // [r]ead
		case 'w': // [w]rite
			err_out(range_init(&global_r, optarg) < 0, "");
			for (; optind < argc && !strcmp(argv[optind], "-f"); optind++); // exhaust through -f
			err_out(optind >= argc, "-%c mode requires an executable\n", c);
			k = optind; // k is the position of first file
			foreach_optarg(argc, argv); // exhaust through files
			l = optind; // r is the position after last file
			for (; optind < argc && strcmp(argv[optind], "-e"); optind++){ // search for -e/-E
				if (!strcmp(argv[optind], "-E")){
					multiple_mode = 1;
					break;
				}
			}
			err_out(optind >= argc - 1, "-%c mode requires an executable\n", c);
			p_exe_path = &argv[++optind];
			exe_argc = argc - optind;
			argv[l] = NULL;
			qsort(&argv[k], l - k, sizeof(char*), p_strcmp);
			for (i = k, j = k + 1; j < l; j++){ // remove dups
				if (strcmp(argv[i], argv[j])){
					i++;
					if (i != j)
						strcpy(argv[i], argv[j]);
				}
			}
			argv[i + 1] = NULL; // cut off after last file after removing dups
			err_out(query_select_named_range(&global_r, &argv[k], 1) < 0, ""); // TODO: NULL means everything
			break;
		case 'n': // i[n]sert
			err_out(range_init(&global_r, optarg) < 0, "");
			while (getopt(argc, argv, "+f:") == 'f'){
				rf = range_add_new_file(&global_r, optarg, ID_NONE);
				err_out(!rf, "Failed capture file %s\n", optarg);
				j = 0;
				foreach_optarg(argc, argv){
					get_range(&base, &bound, argv[optind]);
					err_out(!range_file_add_it(rf, base, bound, ID_NONE), "");
					j++;
				}
				err_out(!j, "No offset specified for file %s\n", rf->file_path);
				i++;
			}
			err_out(!i, "No file specified\n");
			// sql w/ &global_r
			break;
		case 'g': // new ran[g]e
			fs = malloc(argc * sizeof(struct range_file*));
			err_out(!fs, "Too many arguments\n");
			err_out(range_init(&global_r, optarg) < 0, "");
			fs[0] = NULL;
			while (getopt(argc, argv, buf) == buf[1]){
				optind--;
				if (i % 2){ // -r
					for (j = 0, l = 0; fs[j]; j++){
						rf = range_add_new_file(&global_r, (char*)(fs[j]), ID_NONE);
						err_out(!rf, "");
						for (k = 0; k < l; k++){
							if (fs[k] == rf){
								goto skip_add_file;
							}
						}
						fs[l] = rf;
						l++;
skip_add_file:;
					}
					foreach_optarg(argc, argv){
						get_range(&base, &bound, argv[optind]);
						for (j = 0; j < l; j++){
							err_out(!range_file_add_it(fs[j], base, bound, ID_NONE), "");
						}
					}
					buf -= 4;
				}
				else{ // -f
					j = 0;
					foreach_optarg(argc, argv){
						fs[j] = (struct range_file*)(argv[optind]);
						j++;
					}
					err_out(!j, "-f flag without file\n");
					fs[j] = NULL;
					buf += 4;
				}
				i++;
			}
			free(fs);
			err_out(!i, "No file specified\n");
			if (query_insert_named_range(&global_r) < 0){
				fprintf(stderr, "Failed to insert range \"%s\"\n", global_r.name);
			}
			else{
				fprintf(stderr, "Range \"%s\" inserted successfully\n", global_r.name);
			}
			break;
		case 'p': // [p]rint
			thds = malloc(argc * sizeof(pthread_t));
			err_out(!thds, "Too many arguments\n");
			for(;;){
				c = getopt(argc, argv, "+f:g:r:");
				optind--;
				switch (c){
					case 'f':
						foreach_optarg(argc, argv){
							if (pthread_create(&thds[i], NULL, thd_pfile, argv[optind])){
								fprintf(stderr, "Failed to create file print thread %d\n", i);
							}
							i++;
						}
						break;
					case 'g':
					case 'r':
						foreach_optarg(argc, argv){
							if (pthread_create(&thds[i], NULL, thd_prange, argv[optind])){
								fprintf(stderr, "Failed to create range print thread %d\n", i);
							}
							i++;
						}
						break;
					default:
						goto out;
				}
			}
out:
			for (j = 0; j < i; j++){
				pthread_join(thds[j], NULL);
			}
			free(thds);
			break;
		case '?':
			err(1);
			break;
		case -1:
			fprintf(stderr, "No mode specified. Use %s -h for help.\n", argv[0]);
			err(1);
			break;
	}
}

int main(int argc, char* argv[]){
	if (argc < 2){
		print_usage(argv[0]);
		err(0);
	}
	opts(argc, argv);
	err(0);
}
