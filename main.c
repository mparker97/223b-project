#include <stdio.h>
#include <unistd.h>
#include <fnctl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "common.h"
#include "range.h"
#define _GNU_SOURCE

#define CP_BYTES_BUF_SZ 4096

void print_usage(char* s){
	fprintf(stderr, "USAGE: %s MODE [OPTIONS] EXE_PATH [EXE_OPTIONS]\n", s);
}

void print_help(char* s){
	print_usage(s);
	// TODO
	err(0);
}

void cp_bytes(int dst_fd, int src_fd, size_t sz){
	char buf[CP_BYTES_BUF_SZ];
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
}

char* swap_file_path(char* src_dir, char* f_path){ // src_dir is absolute
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

int pull_swap_file(char* swp_dir, char* f_path, struct it_head* h, char* oracle){ // swp_dir is absolute
	int swp_fd, f_fd, i;
	char* s;
	size_t src_pos = 0;
	struct stat src_stat;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	struct it_node* p_itn;
	char path[32];
	f_fd = open(f_path, ORDWR);
	if (f_fd < 0){
		fprintf(stderr, "Failed to open %s\n", f_path);
		err(1);
	}
	swp_fd = open(swp_dir, O_RDWR | O_TMPFILE, S_IRWXU);
	if (swp_fd < 0){
		fprintf("failed to create swap file for %s\n", f_path);
		close(f_fd);
		err(1);
	}
	if (!(s = swap_file_path(swp_dir, f_path))){
		fprintf(stderr, "malloc failed\n");
		err(1);
	}
	
	fstat(f_fd, &src_stat);
	it_foreach_interval(h, i, p_itn){
		cp_bytes(swp_fd, f_fd, p_itn->base - src_pos);
		write(swp_fd, oracle[0], oracle_len[0]);
		cp_bytes(swp_fd, f_fd, p_itn->sz);
		write(swp_fd, oracle[1], oracle_len[1]);
		src_pos = p_itn->base + p_itn->sz;
	}
	cp_bytes(swp_fd, f_fd, src_stat.st_size - pos);
	close(f_fd);
    snprintf(path, 32, "/proc/self/fd/%d", swp_fd);
    linkat(0, path, 0, s, AT_SYMLINK_FOLLOW);
	free(s);
	return swp_fd;
}

void push_swap_file(int swp_fd, char* f_path, struct it_head* h, char* oracle){
	int f_fd, i, j, prev_j;
	struct stat src_stat;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	struct it_node* p_itn;
	char buf[ORACLE_LEN_MAX + 1];
	f_fd = open(f_path, O_RDWR);
	if (f_fd < 0){
		fprintf(stderr, "Failed to open %s\n", f_path);
		err(1);
	}
	it_foreach_interval(h, i, p_itn){
		lseek(f_fd, p_itn->base, SEEK_SET);
		read(f_fd, buf, oracle_len[0]);
		if (!strncmp(buf, oracle[0], oracle_len[0])){
			// missing oracle
		}
		
		// after opening oracle
		
		prev_j = -1;
		for (;;){
			read(f_fd, buf, ORACLE_LEN_MAX + 1);
			if (prev_j){
				if (!strncmp(buf, oracle[2] + prev_j, oracle_len[2] - prev_j)){
					// TODO: found ending oracle at SEEK_CUR - (ORACLE_LEN_MAX + 1) - prev_j
				}
			}
			j = substrn(oracle[2], buf, ORACLE_LEN_MAX + 1);
			if (j >= 0){
				j = (ORACLE_LEN_MAX + 1) - j;
				if (j < oracle_len[2]){
					prev_j = j;
				}
				else{
					// TODO: found ending oracle at SEEK_CUR - j
				}
			}
		}
	}
	// unlink_by_fd(swp_fd); // to remove swap file
	//close(swp_fd);
	close(f_fd):
}

void exec_editor(char* f_path){
	char* tmp;
	int f = fork();
	if (f == 0){
		// squeeze in file between exe and args
		p_exe_path[-1] = p_exe_path[0];
		p_exe_path[0] = f_path;
		execvp(p_exe_path[-1], &p_exe_path[-1]);
		fprintf("Failed to run executable %s\n", tmp);
		err(1);
	}
	else if (f > 0){
		waitpid(f, NULL, 0);
		// TODO: push changes with each save somehow?
	}
	else {
		fprintf(stderr, "fork failed\n");
		err(1);
	}
}

void get_range(size_t* base, size_t* bound, unsigned long* m, char* str, char r_mode){
	*m = IT_NODE_NORMAL;
	switch(r_mode){
		case 's':
			*m = IT_NODE_STRING;
			if ((str = strtok(str, ","))){
				*base = (size_t)str;
				if ((str = strtok(NULL, ","))){
					*bound = (size_t)str;
					return;
				}
			}
			break;
		case 'l':
			*m = IT_NODE_LINE;
		case 'b':
			*base = atol(str);
			if (str = strchr(str, ',')){
				if (str[1]){
					*bound = atol(str + 1);
					if (*bound < *base){
						fprintf(stderr, "Invalid range: base %lu, bound %lu\n", *base, *bound);
						err(1);
					}
					*bound -= *base; // size
					return;
				}
			}
			*bound = (size_t)-1;
			return;
		default:
			fprintf(stderr, "Invalid option %c\n", r_mode);
			err(1);
	}
	fprintf(stderr, "Malformed range\n");
	err(1);
}

void opts1(int argc, char* argv[]){
	int i = -1, j;
	unsigned long m;
	size_t base, size;
	char c, r_mode;
	opts1_m = malloc(argc * sizeof(int));
	if (!opts1_m){
		fprintf(stderr, "Too many arguments\n");
		err(1);
	}
	while ((c = getopt(argc, argv, "+ef:qr:sv")) != -1){
		switch (c){
			case 'v': // [v]erbose
				verbosity = VERBOSE;
				break;
			case 'q': // [q]uiet
				verbosity = QUIET;
				break;
			case 'f': // [f]iles
				if (mode == 'r' || mode == 'w' || mode == 'n'){
					if (optind < argc && argv[optind][0] != '-'){
						fprintf(stderr, "rwn mode may only have one file\n");
						err(1);
					}
				}
				if (i < 0) // came from range option; clear file set
					opts1_m[0] = -1;
				for (optind--; optind < argc && argv[optind][0] != '-'; optind++){
					if ((j = range_add_new_file(input_range, argv[optind], ID_NONE)) < 0){
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
			case 'r': // [r]anges
				r_mode = optarg[0];
				for (; optind < argc && argv[optind][0] != '-'; optind++){
					get_range(&base, &size, &m, argv[optind], r_mode);
					for (i = 0; opts1_m[i] >= 0; i++){
						if (!it_insert(&input_range->files[opts1_m[i]].it, base, size, it_mask_on(ID_NONE, m))){
							err(1);
						}
					}
				}
				i = -1;
				break;
			case 'e': // [e]xecutable; text editor
				p_exe_path = &argv[optind];
				goto out;
			case 's': // range from [s]tandard input
				read_from_stdin = 1;
				break;
			case '?':
				err(1);
				break;
		}
	}
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
	char c = getopt(argc, argv, "+cghinprw");
	switch (c){
		case 'h': // [h]elp
			print_help(argv[0]);
			break;
		case 'r': // [r]ead
		case 'w': // [w]rite
		case 'n': // i[n]sert
		case 'g': // new ran[g]e
		case 'p': // [p]rint
			if (mode != 0){
				fprintf(stderr, "Incompatible options: %c and %c\n", c, mode);
				err(1);
			}
			mode = c;
			break;
		case 'c': // [c]leanup
			
			break;
		case 'i': // [i]nteractive; shell
			shell(argv[0]);
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
	opts0(argc, argv);
	opts1(argc, argv);
	
	// TODO
	return 0;
}
