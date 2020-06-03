#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

char** p_exe_path = NULL;
A_LIST_UNION(struct range, arr, num_ranges, ls) ranges;
A_LIST_UNION(char*, arr, num_files, ls) files;
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

void unlink_by_fd(int fd){
	char buf[32];
	char* path;
    snprintf(buf, 32, "/proc/self/fd/%d", fd);
	path = realpath(buf, NULL);
	err_out(!path, "realpath failed on %s\n", buf);
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

int pull_swap_file(char* swp_dir, struct range_file* rf, char** oracle){ // swp_dir is absolute // oracle with \n at end
	int swp_fd, f_fd;
	char* s;
	struct stat f_stat;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	char path[32];

	f_fd = open(rf->file_path, O_RDWR);
	err_out(f_fd < 0, "Failed to open %s\n", rf->file_path);
	// O_TMPFILE flag is giving a compile error -- it looks like it needs this define:
	// #define _GNU_SOURCE
		// I've had that there the whole time
	swp_fd = open(swp_dir, O_RDWR | O_TMPFILE, S_IRWXU);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to create swap file for %s\n", rf->file_path);
		close(f_fd);
		err(1);
	}
	if (!(s = swap_file_path(swp_dir, rf->file_path))){
		fprintf(stderr, "Failed to malloc swap file name\n");
		close(f_fd);
		close(swp_fd);
		err(1);
	}
	
	fstat(f_fd, &f_stat);
	oracle_len[0]--;
	oracle_len[1]--;
	add_oracle_bytes(swp_fd, f_fd, rf, oracle, oracle_len);
	cp_bytes(swp_fd, f_fd, f_stat.st_size - lseek(f_fd, 0, SEEK_SET));
	
	close(f_fd);
    snprintf(path, 32, "/proc/self/fd/%d", swp_fd);
    linkat(0, path, 0, s, AT_SYMLINK_FOLLOW);
	free(s);
	return swp_fd;
}

void push_swap_file(int swp_fd, struct range_file* rf, char** oracle){ // oracle w/o \n
	int f_fd, i = 0;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	struct it_node* p_itn;
	ssize_t o_open = 0, o_close = -oracle_len[0];//, total_change = 0;
	size_t nl_count = 1;
	f_fd = open(rf->file_path, O_RDWR);
	err_out(f_fd < 0, "Failed to open %s\n", rf->file_path);
	it_foreach(&rf->it, p_itn){
		o_open = oracle_search(swp_fd, oracle[0], oracle_len[0], o_close + oracle_len[0], &nl_count);
		if (o_open < 0){
			fprintf(stderr, "Failed to find opening oracle for range %d\n", i);
			goto rexec;
		}
		/*else if (o_open != p_itn->base + total_change){
			fprintf(stderr, "warning: opening oracle for range %d moved by %ld %s\n",
				i, (p_itn->base + total_change) - o_open, (rf->mode == RANGE_FILE_MODE_NORMAL)? "bytes" : "lines");
		}*/
		o_close = oracle_search(swp_fd, oracle[1], oracle_len[1], o_open + oracle_len[0], &nl_count);
		if (o_close < 0){
			fprintf(stderr, "Failed to find closing oracle for range %d\n", i);
			goto rexec;
		}
		//total_change += o_close - (p_itn->bound);
		// must keep old bound for resize file query
			// However, don't need old base, so replace it with new bound
		p_itn->base = o_close - i * oracle_len[0];
		i++;
	}
	// unlink_by_fd(swp_fd); // to remove swap file
	//close(swp_fd);
	close(f_fd);
rexec:
	exec_editor(rf->file_path); // TODO: maybe move this to caller?
}

void exec_editor(char* f_path){
	char* tmp;
	int f = fork();
	err_out(f < 0, "Fork failed\n");
	if (f == 0){
		// squeeze in file between exe and args
		p_exe_path[-1] = p_exe_path[0];
		p_exe_path[0] = f_path;
		execvp(p_exe_path[-1], &p_exe_path[-1]);
		err_out(true, "Failed to run executable %s\n", tmp);
	}
	else if (f > 0){
		waitpid(f, NULL, 0);
		// TODO: push changes with each save somehow?
		
	}
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

void opts(int argc, char* argv[]){
	struct range_file* rf = NULL;
	size_t base, bound;
	int i = 0, j, k, l;
	struct range_file* fs = NULL;
	char* buf = "+f:\0+r:";
	char** cp;
	char c = getopt(argc, argv, "+g:hn:pr:w:");
	switch (c){
		case 'h': // [h]elp
			print_help(argv[0]);
			break;
		case 'r': // [r]ead
		case 'w': // [w]rite
			err_out(range_init(&ranges.arr[0], optarg) < 0, "");
			if (argv[optind] == '-f'){
				optind++;
			}
			foreach_optarg(argc, argv){
				err_out(range_add_new_file(&ranges.arr[0], argv[optind], ID_NONE) < 0, "Failed to capture file %s\n", argv[optind]);
				i++;
			}
			err_out(!i, "No file specified\n");
			break;
		case 'n': // i[n]sert
			err_out(range_init(&ranges.arr[0], optarg) < 0, "");
			while (getopt(argc, argv, "+f:") == 'f'){
				rf = range_add_new_file(&ranges.arr[0], optarg, ID_NONE);
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
			fs = malloc(argc * sizeof(struct range_file));
			err_out(!fs, "Too many arguments\n");
			err_out(range_init(&ranges.arr[0], optarg) < 0, "");
			fs[0] = NULL;
			while (getopt(argc, argv, buf) == buf[1]){
				optind--;
				if (i % 2){
					for (j = 0, l = 0; fs[j]; j++){
						rf = range_add_new_file(&ranges.arr[0], fs[j], ID_NONE);
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
			err_out(!i, "No file specified\n");
			break;
		case 'p': // [p]rint
			a_list_init(&files->ls, sizeof(char*));
			for(;;){
				c = getopt(argc, argv, "+f:g:r:");
				optind--;
				switch (c){
					case 'f':
						foreach_optarg(argc, argv){
							cp = a_list_add(&files->ls, sizeof(char*));
							err_out(!cp, "Failed to capture file %s\n", argv[optind]);
							*cp = &argv[optind];
						}
						break;
					case 'g':
					case 'r':
						foreach_optarg(argc, argv){
							cp = a_list_add(&ranges->ls, sizeof(struct range*));
							err_out(!cp, "Failed to capture range %s\n", argv[optind]);
							err_out(range_init((struct range*)cp, argv[optind]) < 0, "Failed to capture range %s\n", argv[optind]);
						}
						break;
					default:
						goto out;
				}
			}
			break;
		case '?':
			err(1);
			break;
		case -1:
			fprintf(stderr, "No mode specified. Use %s -h for help\n", argv[0]);
			err(1);
			break;
	}
out:
	if (fs)
		free(fs);
}

int main(int argc, char* argv[]){
	if (argc < 2){
		print_usage(argv[0]);
		err(0);
	}
	a_list_init(&ranges.ls, sizeof(struct range));
	opts(argc, argv);
	
	// TODO
	return 0;
}
