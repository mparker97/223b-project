#include <stdio.h>
#include <unistd.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "file.h"
#include "sql.h"
#include "common.h"
#include "interval_tree.h"
#define CP_BYTES_BUF_SZ 4096

char swp_dir[PATH_MAX];
char* oracle[] = {"/*OPEN_ORACLE*/", "/*CLOSE_ORACLE*/"};

static void cp_bytes(int dst_fd, int src_fd, size_t sz){
	char buf[CP_BYTES_BUF_SZ + 1];
	size_t i;
	ssize_t r;
	for (i = 0; i < sz; i += r){
		r = read(src_fd, buf, CP_BYTES_BUF_SZ);
		if (r < 0){
			fprintf(stderr, "Failed to read from file %d\n", src_fd);
			close(dst_fd);
			close(src_fd);
			err(1);
		}
		else if (r == 0){
			break;
		}
		write(dst_fd, buf, r);
	}
}

static char* swap_file_path(char* src_dir, char* f_path){ // src_dir is absolute // free result
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

static void unlink_by_fd(int fd){
	char* path = NULL;
	get_path_by_fd(path, fd);
	err_out(!path, "Failed to unlink fd %d\n", fd);
    unlink(path);
	free(path);
}

static ssize_t oracle_search(int fd, const char* oracle, size_t oracle_len, size_t off){ // search file identified by descriptor fd for string s of length s_len, starting at offset off
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

static void add_oracle(int swp_fd, int f_fd, struct range_file* rf, char** oracle, size_t* oracle_len){
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
	swp_fd = open(swp_dir, O_RDWR /*| O_TMPFILE*/, S_IRWXU);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to create swap file for %s\n", rf->file_path);
		goto fail;
	}
	
	fstat(backing_fd, &f_stat);
	oracle_len[0]--;
	oracle_len[1]--;
	add_oracle(swp_fd, backing_fd, rf, oracle, oracle_len);
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

int push_swap_file(int swp_fd, struct range_file* rf){
	int ret = 0, backing_fd, i = 0;
	size_t oracle_len[2] = {strlen(oracle[0]), strlen(oracle[1])};
	struct it_node* p_itn;
	ssize_t o_open = 0, o_close = -oracle_len[0];//, total_change = 0;
	backing_fd = open(rf->file_path, O_RDWR); // TODO: ZOOKEEPER HERE?
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		ret = -1;
		goto pass; // really fail
	}
	it_foreach(&rf->it, p_itn){
		o_open = oracle_search(swp_fd, oracle[0], oracle_len[0], o_close + oracle_len[0]);
		if (o_open < 0){
			fprintf(stderr, "Failed to find opening oracle:\n\t%s\nfor range %d\n", oracle[0], i);
			goto rexec;
		}
		/*else if (o_open != p_itn->base + total_change){
			fprintf(stderr, "warning: opening oracle for range %d moved by %ld %s\n",
				i, (p_itn->base + total_change) - o_open, (rf->mode == RANGE_FILE_MODE_NORMAL)? "bytes" : "lines");
		}*/
		o_close = oracle_search(swp_fd, oracle[1], oracle_len[1], o_open + oracle_len[0]);
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
	// TODO: need where region starts in swap file?
	if (query_resize_file(rf, swp_fd, backing_fd) >= 0){
		unlink_by_fd(swp_fd); // to remove swap file
	}
	close(swp_fd);
	goto pass;
rexec:
	ret = -2;
pass:
	close(backing_fd);
	return ret;
}

int write_offset_update(struct offset_update* ou, int len, int swp_fd, int backing_fd){
	int i;
	for (i = 0; i < len; i++){
		lseek(swp_fd, ou[i].swp_start, SEEK_SET);
		// TODO
	}
}