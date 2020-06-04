#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "common.h"
#include "range.h"
//#include "_regex.h"
#include "help.h"
#include "zkclient.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define CP_BYTES_BUF_SZ 4096

#define foreach_optarg(argc, argv) for (; optind < (argc) && (argv)[optind][0] != '-'; optind++)

char** p_exe_path;
struct range* global_r;
//A_LIST_UNION(char*, arr, num_files, ls) files;
struct range_file global_rf;
pthread_mutex_t print_lock;
static char swp_dir[PATH_MAX];
static char oracle[] = {"/*OPEN_ORACLE*/", "/*CLOSE_ORACLE*/"};
static char mode = 0;

void cp_bytes(int dst_fd, int src_fd, size_t sz){
	char buf[CP_BYTES_BUF_SZ + 1];
	size_t i;
	ssize_t r;
	for (i = 0; i < sz; i += r){
		r = read(src_fd, buf, CP_BYTES_BUF_SZ);
		if (r < 0){
			fprintf(stderr, "Failed to read from file %d\n", src_fd);
			closec(dst_fd);
			closec(src_fd);
			err(1);
		}
		write(dst_fd, buf, r);
	}
	return line_count;
}

char* swap_file_path(char* src_dir, char* f_path){ // src_dir is absolute // free result
	char* ret, *s;
	s = strrchr(f_path, '/');
	if (!(ret = malloc(strlen(src_dir) + strlen(s) + 5))){
		return NULL;
	}
	sprintf(ret, "%s%s.swp", src_dir, s);
	return ret;
}

void get_path_by_fd(char* path, int fd){
	char buf[32];
    snprintf(buf, 32, "/proc/self/fd/%d", fd);
	path = realpath(buf, NULL);
}

void unlink_by_fd(int fd){
	char* path = NULL;
	get_path_by_fd(path, fd):
	err_out(!path, "Failed to unlink fd %d\n", fd);
    unlink(path);
	free(path);
}

ssize_t oracle_search(int fd, const char* oracle, size_t oracle_len, size_t off){ // search file identified by descriptor fd for string s of length s_len, starting at offset off
	char* line = NULL, *str;
	size_t ret = off, n;
	ssize_t s;
	FILE* f = fdopen(fd, "r");
	if (f){
		fseek(f, off, SEEK_SET);
		for (line = NULL; (s = getline(&line, &n, f)) >= 0; ret += s){
			str = strstr(line, oracle);
			if (str){
				return ret + (str - line);
			}
		}
		free(line);
	}
	return -1;
}

void add_oracle_bytes(int swp_fd, int f_fd, struct range_file* rf, char** oracle, size_t* oracle_len){
	struct it_node* p_itn;
	size_t src_pos = 0;
	it_foreach(&rf->it, p_itn){
		cp_bytes(swp_fd, f_fd, p_itn->base - src_pos);
		write(swp_fd, oracle[0], oracle_len[0]);
		cp_bytes(swp_fd, f_fd, p_itn->bound - p_itn->base);
		write(swp_fd, oracle[1], oracle_len[1]);
		src_pos = p_itn->bound;
	}
}

int pull_swap_file(struct range_file* rf){
	int swp_fd = 0, backing_fd = 0;
	char* s;
	struct stat f_stat;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	char path[32];

	if (!(s = swap_file_path(swp_dir, rf->file_path))){
		fprintf(stderr, "Failed to malloc swap file name for %s\n", rf->file_path);
		goto fail;
	}
	backing_fd = open(rf->file_path, O_RDWR); // TODO: ZOOKEEPER HERE?
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		goto fail;
	}
	swp_fd = open(swp_dir, O_RDWR | O_TMPFILE, S_IRWXU);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to create swap file for %s\n", rf->file_path);
		goto fail;
	}
	
	fstat(backing_fd, &f_stat);
	oracle_len[0]--;
	oracle_len[1]--;
	add_oracle_bytes(swp_fd, backing_fd, rf, oracle, oracle_len);
	cp_bytes(swp_fd, backing_fd, f_stat.st_size - lseek(backing_fd, 0, SEEK_SET));
	
	close(backing_fd);
    snprintf(path, 32, "/proc/self/fd/%d", swp_fd);
    linkat(0, path, 0, s, AT_SYMLINK_FOLLOW);
	free(s);
	return swp_fd;
fail:
	if (backing_fd >= 0)
		close(backing_fd);
	if (swp_fd >= 0);
		close(swp_fd);
	if (s)
		free(s);
	err(1);
	return -1;
}

int push_swap_file(int swp_fd, struct range_file* rf){ // oracle w/o \n
	int ret = 0, backing_fd, i = 0;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	struct it_node* p_itn;
	ssize_t o_open = 0, o_close = -oracle_len[0];//, total_change = 0;
	size_t nl_count = 1;
	backing_fd = open(rf->file_path, O_RDWR); // TODO: ZOOKEEPER HERE?
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		ret = -1;
		goto pass; // really fail
	}
	it_foreach(&rf->it, p_itn){
		o_open = oracle_search(swp_fd, oracle[0], oracle_len[0], o_close + oracle_len[0], &nl_count);
		if (o_open < 0){
			fprintf(stderr, "Failed to find opening oracle:\n\t%s\nfor range %d\n", oracle[0], i);
			goto rexec;
		}
		/*else if (o_open != p_itn->base + total_change){
			fprintf(stderr, "warning: opening oracle for range %d moved by %ld %s\n",
				i, (p_itn->base + total_change) - o_open, (rf->mode == RANGE_FILE_MODE_NORMAL)? "bytes" : "lines");
		}*/
		o_close = oracle_search(swp_fd, oracle[1], oracle_len[1], o_open + oracle_len[0], &nl_count);
		if (o_close < 0){
			fprintf(stderr, "Failed to find closing oracle:\n\t%s\nfor range %d\n", oracle[1], i);
			goto rexec;
		}
		//total_change += o_close - (p_itn->bound);
		// must keep old bound for resize file query
			// However, don't need old base, so replace it with new bound
			// Bound stays as the old bound
		p_itn->base = o_close - i * oracle_len[0];
		i++;
	}
	// unlink_by_fd(swp_fd); // to remove swap file
	//close(swp_fd);
	goto pass;
rexec:
	ret = -2;
pass:
	close(backing_fd);
	return ret;
}

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
		waitpid(f, NULL, 0);
	}
	return f;
}

struct open_files_thread{
	pthread_t thd;
	struct range_file* rf;
};

void* open_files_func(void* arg){
	char path[PATH_MAX];
	struct open_files_thread* oft = (struct open_files_thread*)arg;
	int i, swp_fd;
	swp_fd = pull_swap_file(oft->rf);
	if (swp_fd < 0){
		fprintf(stder, "Failed to pull file %s\n", oft->rf->file_path);
		goto fail;
	}
	get_path_by_fd(path, swp_fd);
	if (!path){
		fprintf(stderr, "Failed to resolve swap file path for %s\n", oft->rf->file_path);
		goto fail;
	}
	do{
		if (exec_editor(path) < 0){
			goto fail;
		}
		i = push_swap_file(swp_fd, oft->rf);
		if (i == -1){
			goto fail;
		}
	} while (i < 0);
	if (query_resize_file(oft->rf) < 0){
		goto fail;
	}
	return (void*)1;
fail:
	return NULL;
}

void open_files(struct range* r){
	int i, j;
	int* thds = malloc(r->num_files * sizeof(struct open_files_thread));
	int* retval;
	err_out(!thds, "Malloc open files threads failed\n");
	for (i = 0; i < r->num_files; i++){
		thds[i].rf = &r->files[i];
		pthread_create(&thds[i].thd, NULL, open_files_func, &thds[i]);
		
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
	range_init(&r);
	if (query_select_named_range(&r) >= 0){
		do_print_range(&r);
	}
	else{
		fprintf(stderr, "Unable to print range %s\n", r.name);
	}
	return NULL;
}

void* thd_pfile(void* arg){
	struct range_file rf;
	char* name = (char*)arg;
	it_init(&rf.it);
	if (query_select_file_intervals(&rf, name) >= 0){
		do_print_file(&rf);
	}
	else{
		fprintf(stderr, "Unable to print file %s\n", r.name);
	}
	return NULL;
}

void opts(int argc, char* argv[]){
	struct range_file* rf = NULL;
	size_t base, bound;
	int i = 0, j, k, l;
	struct range_file** fs = NULL;
	char* buf = "+f:\0+r:";
	pthread_t* thds = NULL;
	char c = getopt(argc, argv, "+g:hn:pr:w:");
	switch (c){
		case 'h': // [h]elp
			print_help(argv[0]);
			break;
		case 'r': // [r]ead
		case 'w': // [w]rite
			err_out(range_init(&global_r, optarg) < 0, "");
			if (argv[optind] == '-f'){
				optind++;
			}
			/*foreach_optarg(argc, argv){
				cp = a_list_add(&files.ls, sizeof(char*));
				err_out(!cp, "Failed to capture file %s\n", argv[optind]);
				*cp = &argv[optind];
				i++;
			}*/
			err_out(query_select_named_range(&global_r) < 0);
			/*if (i){
				a_list_sort(&files.ls, sizeof(char*), COMP_STRING);
				
			}*/
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
					err_out(!it_insert(&rf->it, base, bound, ID_NONE), "");
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
				if (i % 2){
					for (j = 0, l = 0; fs[j]; j++){
						rf = range_add_new_file(&global_r, fs[j], ID_NONE);
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
							err_out(!it_insert(&fs[j]->it, base, bound, ID_NONE), "");
						}
					}
					buf -= 4;
				}
				else{
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
				switch (c){ // I realize I could have just printed them on the fly... oh well
					case 'f':
						foreach_optarg(argc, argv){
							if (!pthread_create(&thds[i], NULL, thd_pfile, argv[optind])){
								printf(stderr, "Failed to create file print thread %d\n", i);
							}
							i++;
						}
						break;
					case 'g':
					case 'r':
						foreach_optarg(argc, argv){
							if (!pthread_create(&thds[i], NULL, thd_rfile, argv[optind])){
								printf(stderr, "Failed to create range print thread %d\n", i);
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
				pthread_join(thds[j], fs); // I don't care for retval
			}
			free(thds);
			break;
		case '?':
			err(1);
			break;
		case -1:
			fprintf(stderr, "No mode specified. Use %s -h for help\n", argv[0]);
			err(1);
			break;
	}
}

int main(int argc, char* argv[]){
	if (argc < 2){
		print_usage(argv[0]);
		err(0);
	}
	err_out(!getcwd(buf, PATH_MAX)
		|| range_init(&global_r) < 0
		|| pthread_mutex_init(print_lock),
		"Failed to initialize\n");
	it_init(&global_rf.it);
	opts(argc, argv);
	
	// TODO?
	return 0;
}
