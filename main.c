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

int* opts1_m = NULL;
char** p_exe_path = NULL;
A_LIST_UNION(struct range, arr, num_ranges, ls) ranges;
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
	if (!(path = realpath(buf, NULL))){
		fprintf(stderr, "realpath failed on %s\n", buf);
		err(1);
	}
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
	if (f_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		err(1);
	}
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
	if (f_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		err(1);
	}
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
	if (f == 0){
		// squeeze in file between exe and args
		p_exe_path[-1] = p_exe_path[0];
		p_exe_path[0] = f_path;
		execvp(p_exe_path[-1], &p_exe_path[-1]);
		fprintf(stderr, "Failed to run executable %s\n", tmp);
		err(1);
	}
	else if (f > 0){
		waitpid(f, NULL, 0);
		// TODO: push changes with each save somehow?
		
	}
	else {
		fprintf(stderr, "Fork failed\n");
		err(1);
	}
}

void get_range(size_t* base, size_t* bound, char* str){
	*base = atol(str);
	if (str = strchr(str, ',')){
		if (str[1]){
			*bound = atol(str + 1);
			if (*bound <= *base){
				fprintf(stderr, "Invalid region: base %lu, bound %lu\n", *base, *bound);
				err(1);
			}
			*bound;
			return;
		}
	}
	fprintf(stderr, "Malformed region: \"%s\"\n", str);
	err(1);
}

void opts1(int argc, char* argv[]){
	int i = -1, j;
	size_t base, bound;
	char c;
	opts1_m = malloc(argc * sizeof(int));
	if (!opts1_m){
		fprintf(stderr, "Too many arguments\n");
		err(1);
	}
	while ((c = getopt(argc, argv, "+e:f:qr:sv")) != -1){
		// TODO: separate op strings for each mode!
		switch (c){
			case 'f': // [f]iles
				switch (mode){
					// TODO: figure out what is allowed for which modes
					case 'n':
						if (optind < argc && argv[optind][0] != '-'){ // argument after the argument for '-f' is NOT another option argument: this implies more than one argument for '-f'
							fprintf(stderr, "n mode may only have one file\n");
							err(1);
						}
					case 'g':
						if (i < 0){ // came from range option; clear file set
							opts1_m[0] = -1;
						}
						optind--;
						foreach_optarg(argc, argv){
							j = range_add_new_file(&ranges.arr[0], argv[optind], ID_NONE);
							if (j < 0){
								fprintf(stderr, "Failed to add file %s\n", argv[optind]);
								err(1);
							}
							for (i = 0; opts1_m[i] >= 0; i++){
								if (opts1_m[i] == j){
									goto skip; // file already in this set; skip
								}
							}
							opts1_m[i] = j; // new file for this set; add to end
							opts1_m[i + 1] = -1;
skip:;
						}
						if (opts1_m[0] < 0){
							fprintf(stderr, "%s: option requires an argument -- 'r'\n", argv[0]);
							err(1);
						}
						break;
					case 'w':
					case 'p':
						fprintf(stderr, "Invalid option 'f' for mode '%c'\n", mode);
						err(1);
						break;
				}
				break;
			case 'r': // [r]anges
				switch (mode){
					case 'n':
					case 'g':
						if (ranges.num_ranges == 0){
							fprintf(stderr, "No range name specified\n");
							err(1);
						}
						foreach_optarg(argc, argv){
							get_range(&base, &bound, argv[optind]);
							for (i = 0; opts1_m[i] >= 0; i++){
								if (!it_insert(&(ranges.arr[0].files[opts1_m[i]].it), base, bound)){
									fprintf(stderr, "Failed to insert region [%lu, %lu)\n", base, bound); // only due to malloc as of now
									err(1);
								}
							}
						}
						i = -1;
						break;
					case 'w':
					case 'p':
						fprintf(stderr, "Invalid option 'r' for mode '%c'\n", mode);
						err(1);
						break;
				}
				break;
			case 'e': // [e]xecutable; text editor
				p_exe_path = &argv[optind];
				goto out;
			case '?':
				err(1);
				break;
		}
	}
	/*if (mode == 'p'){
		for (i = 0; i < ranges.num_ranges; i++){
			do_print_range(&ranges.arr[i]);
		}
	}*/
	if (optind < argc){
		if (p_exe_path == NULL){
			p_exe_path = &argv[optind]; // rest are exe args
		}
	}
	if (p_exe_path == NULL){
		fprintf(stderr, "Executable not specified\n");
		print_usage(argv[0]);
		err(0);
	}
	freec(opts1_m);
	
}

void opts0(int argc, char* argv[]){
	struct range* r, i;
	char c = getopt(argc, argv, "+cg:hin:p:r:w:");
	switch (c){
		case 'h': // [h]elp
			print_help(argv[0]);
			break;
		case 'r': // [r]ead
		case 'w': // [w]rite
		case 'n': // i[n]sert
		case 'g': // new ran[g]e
		case 'p': // [p]rint
			if (mode != 0 && mode != c){
				fprintf(stderr, "Incompatible options: %c and %c\n", c, mode);
				err(1);
			}
			if (c != 'p' && optind < argc && argv[optind][0] != '-'){
				fprintf(stderr, "Only one named range may be selected for mode %c\n", c);
				err(1);
			}
			mode = c;
			optind--;
			foreach_optarg(argc, argv){
				for (i = 0; i < ranges.num_ranges; i++){
					if (!strcmp(ranges.arr[i].name, argv[optind])){
						goto next_arg;
					}
				}
				r = a_list_addc(&ranges.ls, sizeof(struct range));
				if (r == NULL){
					fprintf(stderr, "Failed to add named range %s\n", argv[optind]);
					err(1);
				}
				r->name = argv[optind];
				r->id = ID_NONE;
next_arg:;
			}
			break;
		case 'c': // [c]leanup
			
			break;
		case 'i': // [i]nteractive; shell
			//shell(argv[0]);
			err(0);
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
	a_list_init(&ranges.ls, sizeof(struct range));
	opts0(argc, argv);
	opts1(argc, argv);
	
	// TODO
	return 0;
}
