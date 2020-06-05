#include <stdio.h>
#include <unistd.h>
//#include <fcntl.h>
//#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <wait.h>
#include "file.h"
#include "sql.h"
#include "common.h"
#include "range.h"
#include "help.h"
#include "zkclient.h"

#define foreach_optarg(argc, argv) for (; optind < (argc) && (argv)[optind][0] != '-'; optind++)

extern char swp_dir[PATH_MAX];
struct range global_r;
struct range_file global_rf;
pthread_mutex_t print_lock;
static char** p_exe_path;
static char mode = 0;

int exec_editor(char* f_path){
	char* tmp;
	int f = fork();
	if (f < 0){
		fprintf(stderr, "Fork failed\n");
		return -1;
	}
	if (f == 0){
		// squeeze in file between exe and args
		p_exe_path[-1] = p_exe_path[0];
		p_exe_path[0] = f_path;
		execvp(p_exe_path[-1], &p_exe_path[-1]);
		err_out(true, "Failed to run executable %s\n", tmp);
	}
	else{
		printf("Writing file %s from pid %d\n", f_path, f);
		waitpid(f, NULL, 0);
	}
	return f;
}

struct open_files_thread{
	pthread_t thd;
	struct range_file* rf;
};

void* thd_open_files(void* arg){
	char path[PATH_MAX];
	struct open_files_thread* oft = (struct open_files_thread*)arg;
	int i, swp_fd;
	swp_fd = pull_swap_file(oft->rf);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to pull file %s\n", oft->rf->file_path);
		goto fail;
	}
	get_path_by_fd(path, swp_fd);
	if (!path){
		fprintf(stderr, "Failed to resolve swap file path for %s\n", oft->rf->file_path);
		goto fail;
	}
	do{
		// TODO: can I keep the swap file desc open while I do exec?
		if (exec_editor(path) < 0){
			goto fail;
		}
		i = push_swap_file(swp_fd, oft->rf);
		if (i == -1){
			goto fail;
		}
	} while (i < 0);
	return (void*)1;
fail:
	return NULL;
}

void open_files(struct range* r){
	int i, j;
	struct open_files_thread* thds = malloc(r->num_files * sizeof(struct open_files_thread));
	void* retval;
	err_out(!thds, "Malloc open files threads failed\n");
	for (i = 0; i < r->num_files; i++){
		thds[i].rf = &r->files[i];
		pthread_create(&thds[i].thd, NULL, thd_open_files, &thds[i]);
	}
	for (i = 0; i < r->num_files; i++){
		pthread_join(thds[i].thd, &retval);
	}
	if (thds)
		free(thds);
}

void get_range(size_t* base, size_t* bound, char* str){
	*base = atol(str);
	if (str = strchr(str, ',')){
		if (str[1]){
			*bound = atol(str + 1);
			err_out(*bound <= *base, "Invalid region: base %lu, bound %lu\n", *base, *bound);
			return;
		}
		*base = (size_t)(-1);
	}
	fprintf(stderr, "Malformed region: \"%s\"\n", str);
	err(1);
}

void* thd_prange(void* arg){
	struct range r;
	char* name = (char*)arg;
	range_init(&r, name);
	if (query_select_named_range(&r) >= 0){
		do_print_range(&r);
	}
	else{
		fprintf(stderr, "Unable to print range %s\n", name);
	}
	range_deinit(&r);
	return NULL;
}

void* thd_pfile(void* arg){
	struct range_file rf;
	char* name = (char*)arg;
	it_init(&rf.it);
	if (query_select_file_intervals(&rf, name, NULL) >= 0){
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
	void* retval;
	char* buf = "+f:\0+r:";
	pthread_t* thds = NULL;
	int i = 0, j, k, l;
	char c = getopt(argc, argv, "+g:hn:pr:w:");
	switch (c){
		case 'h': // [h]elp
			print_help(argv[0]);
			break;
		case 'r': // [r]ead
		case 'w': // [w]rite
			err_out(range_init(&global_r, optarg) < 0, "");
			if (!strncmp(argv[optind], "-f", 2)){
				optind++;
			}
			err_out(query_select_named_range(&global_r) < 0, "");
			open_files(&global_r);
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
						fs[j] = (struct range_file*)(argv[j]);
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
			query_insert_named_range(&global_r);
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
							if (!pthread_create(&thds[i], NULL, thd_pfile, argv[optind])){
								fprintf(stderr, "Failed to create file print thread %d\n", i);
							}
							i++;
						}
						break;
					case 'g':
					case 'r':
						foreach_optarg(argc, argv){
							if (!pthread_create(&thds[i], NULL, thd_prange, argv[optind])){
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
				pthread_join(thds[j], &retval);
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
	err_out(!getcwd(swp_dir, PATH_MAX)
		|| sql_init() < 0
		|| pthread_mutex_init(&print_lock, NULL),
		"Failed to initialize\n");
	it_init(&global_rf.it);
	opts(argc, argv);
	err(0);
}
