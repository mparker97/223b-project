#include <stdio.h>
#include <unistd.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "file.h"
#include "options.h"
#include "sql.h"
#include "common.h"
#include "interval_tree.h"
#include "zkclient.h"

// not const b/c discard qualifier
char* DEFAULT_START_ORACLE = "[START ORACLE]";
char* DEFAULT_END_ORACLE = "[END ORACLE]";

char swp_dir[PATH_MAX + 1];

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
	if (!path){
		fprintf(stderr, "Failed to unlink fd %d\n", fd);
	}
	else{
		unlink(path);
		free(path);
	}
}

static ssize_t oracle_search(int fd, const char* oracle, size_t oracle_len, size_t off){ // search file identified by descriptor fd for string s of length s_len, starting at offset off
	char* line, *str;
	size_t ret = off, n;
	ssize_t s;
	FILE* f = fdopen(fd, "r");
	if (f){
		fseek(f, off, SEEK_SET);
		for (line = NULL; (s = getline(&line, &n, f)) >= 0; ret += s){
			str = strstr(line, oracle);
			if (str){
				free(line);
				return ret + (str - line);
			}
		}
		free(line);
	}
	return -1;
}

static void add_oracles(int swp_fd, int f_fd, struct range_file* rf, struct oracles* o){
	struct it_node* p_itn;
	struct stat st;
	size_t swp_pos = 0, adj_bound;
	ssize_t cond = 0;
	fstat(f_fd, &st);
	lseek(swp_fd, 0, SEEK_SET);
	lseek(f_fd, 0, SEEK_SET);
	it_foreach(&rf->it, p_itn){
		if (cond < 0)
			break;
		if (p_itn->bound == BOUND_END){
			adj_bound = st.st_size;
		}
		else{
			adj_bound = p_itn->bound;
		}
		sendfile(swp_fd, f_fd, NULL, p_itn->base - swp_pos);
		write(swp_fd, o->oracle[0], o->oracle_len[0]);
		cond = sendfile(swp_fd, f_fd, NULL, adj_bound - p_itn->base);
		write(swp_fd, o->oracle[1], o->oracle_len[1]);
		swp_pos = p_itn->bound;
	}
	sendfile(swp_fd, f_fd, NULL, st.st_size - lseek(f_fd, 0, SEEK_CUR));
}

int pull_swap_file(struct range_file* rf, struct oracles* o){
	int swp_fd = 0, backing_fd = 0;
	char* s;
	struct stat f_stat;
	it_node_t zkcontext;
	//char path[32];

	if (!(s = swap_file_path(swp_dir, rf->file_path))){
		fprintf(stderr, "Failed to malloc swap file name for %s\n", rf->file_path);
		goto fail;
	}
	
	get_oracles(o, rf->file_path);
	
	// master read lock
	zkcontext.lock_type = LOCK_TYPE_MASTER_READ;
	zkcontext.file_path = rf->file_path;
	pthread_mutex_init(&(zkcontext.pmutex), NULL);
	if (zk_acquire_lock(&zkcontext) != ZOK){
		goto fail;
	}
	if (!zkcontext.lock_acquired) {
		// watcher in zkclient will unlock once lock gets acquired
		pthread_mutex_lock(&(zkcontext.pmutex));
	}

	backing_fd = open(rf->file_path, O_RDWR);

	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		goto fail;
	}
	//swp_fd = open(swp_dir, O_RDWR /*| O_TMPFILE*/, S_IRWXU);
	swp_fd = open(s, O_RDWR | O_CREAT, S_IRWXU);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to create swap file for %s\n", rf->file_path);
		goto fail;
	}
	
	fstat(backing_fd, &f_stat);
	//oracle_len[0]--;
	//oracle_len[1]--;
	add_oracles(swp_fd, backing_fd, rf, o);
	
	close(backing_fd);
	// successfully read - release master read lock
	zk_release_lock(&zkcontext);

    //snprintf(path, 32, "/proc/self/fd/%d", swp_fd);
    //linkat(0, path, 0, s, AT_SYMLINK_FOLLOW);
	free(s);
	fsync(swp_fd);
	return swp_fd;
fail:
	if (backing_fd >= 0) {
		close(backing_fd);
		// successfully read - release master read lock
		zk_release_lock(&zkcontext);
	}
	if (swp_fd >= 0);
		close(swp_fd);
	if (s)
		free(s);
	return -1;
}

int push_swap_file(int swp_fd, struct range_file* rf, struct oracles* o){
	int ret = 0, backing_fd, i = 0;
	struct it_node* p_itn;
	ssize_t o_open = 0, o_close = -o->oracle_len[1];//, total_change = 0;
	
	// master write lock
	it_node_t zkcontext;
	zkcontext.lock_type = LOCK_TYPE_MASTER_WRITE;
	zkcontext.file_path = rf->file_path;
	pthread_mutex_init(&(zkcontext.pmutex), NULL);

	if (zk_acquire_lock(&zkcontext) != ZOK){
		goto pass;
	}
	if (!zkcontext.lock_acquired) {
		// watcher in zkclient will unlock once lock gets acquired
		pthread_mutex_lock(&(zkcontext.pmutex));
	}

	backing_fd = open(rf->file_path, O_RDWR);
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		ret = -1;
		goto pass; // really fail
	}
	it_foreach(&rf->it, p_itn){
		o_open = oracle_search(swp_fd, o->oracle[0], o->oracle_len[0], o_close + o->oracle_len[1]);
		if (o_open < 0){
			fprintf(stderr, "Failed to find opening oracle:\n\t%s\nfor range %d\n", o->oracle[0], i);
			goto rexec;
		}
		/*else if (o_open != p_itn->base + total_change){
			fprintf(stderr, "warning: opening oracle for range %d moved by %ld %s\n",
				i, (p_itn->base + total_change) - o_open, (rf->mode == RANGE_FILE_MODE_NORMAL)? "bytes" : "lines");
		}*/
		o_close = oracle_search(swp_fd, o->oracle[1], o->oracle_len[1], o_open + o->oracle_len[0]);
		if (o_close < 0){
			fprintf(stderr, "Failed to find closing oracle:\n\t%s\nfor range %d\n", o->oracle[1], i);
			goto rexec;
		}
		//total_change += o_close - (p_itn->bound);
		p_itn->base = o_open - i * (o->oracle_len[0] + o->oracle_len[1]);
		p_itn->bound = o_close - i * (o->oracle_len[0] + o->oracle_len[1]) - o->oracle_len[0];
		i++;
	}
	if (query_resize_file(rf, swp_fd, backing_fd, o) >= 0){
		unlink_by_fd(swp_fd); // to remove swap file
	}
	close(swp_fd);
	goto pass;
rexec:
	ret = -2;
pass:
	close(backing_fd);
	zk_release_lock(&zkcontext);
	return ret;
}

static int exec_editor(char* f_path){
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
		fprintf(stderr, "Failed to run executable %s\n", tmp);
		return -1;
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

static void* thd_open_files(void* arg){
	char path[PATH_MAX];
	struct open_files_thread* oft = (struct open_files_thread*)arg;
	struct oracles o;
	int i, swp_fd;
	swp_fd = pull_swap_file(oft->rf, &o);
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
		i = push_swap_file(swp_fd, oft->rf, &o);
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

int write_offset_update(struct offset_update* ou, int len, int swp_fd, int backing_fd, struct oracles* o){
	// TODO: add back in oracle lengths?
	int ret = 0, i, tmp_fd;
	off_t pos = 0, o_pos = o->oracle_len[0];
	struct stat st;
	tmp_fd = open(swp_dir, O_RDWR | O_TMPFILE | O_EXCL, S_IRWXU);
	if (tmp_fd < 0){
		fprintf(stderr, "Failed to create backing swap file\n");
		goto fail;
	}
	fstat(backing_fd, &st);
	lseek(backing_fd, 0, SEEK_SET);
	for (i = 0; i < len; i++){
		sendfile(tmp_fd, backing_fd, NULL, ou[i].backing_start - pos);
		pos = lseek(backing_fd, ou[i].backing_end, SEEK_SET);
		lseek(swp_fd, ou[i].swp_start + o_pos, SEEK_SET);
		sendfile(tmp_fd, swp_fd, NULL, ou[i].swp_end - ou[i].swp_start);
		o_pos += o->oracle_len[0] + o->oracle_len[1]; // this and next iteration
	}
	sendfile(tmp_fd, backing_fd, NULL, st.st_size - pos);
	fstat(tmp_fd, &st);
	lseek(tmp_fd, 0, SEEK_SET);
	sendfile(backing_fd, tmp_fd, NULL, st.st_size);
	ftruncate(backing_fd, st.st_size);
	fsync(backing_fd);
goto pass;
fail:
	ret = -1;
pass:
	if (tmp_fd >= 0)
		close(tmp_fd);
	return ret;
}