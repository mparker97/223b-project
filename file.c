#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <unistd.h>
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
#include "pcq.h"

// not const b/c discard qualifier
char* DEFAULT_START_ORACLE = "[START ORACLE]";
char* DEFAULT_END_ORACLE = "[END ORACLE]";

char swp_dir[PATH_MAX + 1];
struct pcq global_qp, global_qc;

static char* swap_file_path(char* src_dir, char* f_path){ // src_dir is absolute // free result
	char* ret, *s;
	s = strrchr(f_path, '/');
	if (!(ret = malloc(strlen(src_dir) + strlen(s) + 5))){
		return NULL;
	}
	sprintf(ret, "%s%s.swp", src_dir, s);
	return ret;
}

static int get_path_by_fd(char* path, int fd){
	char buf[32];
    snprintf(buf, 32, "/proc/self/fd/%d", fd);
	if (realpath(buf, path)){
		return 1;
	}
	return 0;
}

static void unlink_by_fd(int fd){
	char path[PATH_MAX + 1];
	if (!get_path_by_fd(path, fd)){
		fprintf(stderr, "Failed to unlink fd %d\n", fd);
	}
	else{
		unlink(path);
	}
}

static ssize_t oracle_search(int swp_fd, const char* oracle, size_t oracle_len, size_t off){ // search file identified by descriptor fd for string s of length s_len, starting at offset off
	char* line, *str;
	size_t ret = off, n;
	ssize_t s;
	FILE* f = fdopen(swp_fd, "r");
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
	
	fail_check(zk_acquire_master_lock(&zkcontext, rf, LOCK_TYPE_MASTER_READ) >= 0);

	backing_fd = open(rf->file_path, O_RDWR | O_CLOEXEC);
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open %s\n", rf->file_path);
		goto fail;
	}
	//swp_fd = open(swp_dir, O_RDWR /*| O_TMPFILE*/, S_IRWXU);
	swp_fd = open(s, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to create swap file for %s\n", rf->file_path);
		goto fail;
	}
	
	fstat(backing_fd, &f_stat);
	//oracle_len[0]--;
	//oracle_len[1]--;
	add_oracles(swp_fd, backing_fd, rf, o);
	
	closec(backing_fd);
	// successfully read - release master read lock
	zk_release_lock(&zkcontext);

    //snprintf(path, 32, "/proc/self/fd/%d", swp_fd);
    //linkat(0, path, 0, s, AT_SYMLINK_FOLLOW);
	free(s);
	fsync(swp_fd);
	return swp_fd;
fail:
	if (backing_fd >= 0) {
		closec(backing_fd);
		zk_release_lock(&zkcontext); // successfully read - release master read lock
	}
	if (swp_fd >= 0)
		closec(swp_fd);
	if (s)
		free(s);
	return -1;
}

int push_swap_file(char* swp_path, struct range_file* rf, struct oracles* o){
	int ret = 0, backing_fd = -1, swp_fd = -1, i = 0, swp_unlink = 1;
	it_node_t zkcontext;
	struct it_node* p_itn;
	ssize_t o_open = 0, o_close = -o->oracle_len[1];//, total_change = 0;
	
	swp_fd = open(swp_path, O_RDWR);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to open swap file %s\n", swp_path);
		ret = -1;
		goto fail;
	}
	
	fail_check(zk_acquire_master_lock(&zkcontext, rf, LOCK_TYPE_MASTER_WRITE) >= 0);

	backing_fd = open(rf->file_path, O_RDWR | O_CLOEXEC);
	if (backing_fd < 0){
		fprintf(stderr, "Failed to open backing file %s\n", rf->file_path);
		ret = -1;
		goto fail;
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
		p_itn->base = o_open + o->oracle_len[0];
		p_itn->bound = o_close;
		i++;
	}

	query_resize_file(rf, swp_fd, backing_fd, o);
	goto fail; // really pass
rexec:
	swp_unlink = 0;
	ret = -2;
fail:
	if (swp_fd >= 0){
		if (swp_unlink)
			unlink_by_fd(swp_fd); // to remove swap file
		closec(swp_fd);
	}
	if (backing_fd >= 0)
		closec(backing_fd);
	zk_release_lock(&zkcontext);
	return ret;
}

static int exec_editor_fork(struct open_files_thread* oft){
	char** c;
	int i, j;
	if (!multiple_mode){
		// squeeze in file between exe and args
		p_exe_path[-1] = p_exe_path[0];
		p_exe_path[0] = oft->swp_file_path;
		p_exe_path--;
	}
	else{
		c = p_exe_path;
		p_exe_path = malloc((exe_argc + oft->i + 1) * sizeof(char*)); // plus 1 for NULL
		if (!p_exe_path){
			fprintf(stderr, "Failed to malloc multiple write mode argv\n");
			return -1;
		}
		p_exe_path[0] = c[0];
		for (i = 0, j = 1; i < oft->i; i++){
			if (oft[i].rf){
				p_exe_path[j++] = oft[i].swp_file_path;
			}
		}
		memcpy(&p_exe_path[j], &c[1], exe_argc);
	}
	execvp(p_exe_path[0], p_exe_path);
	return -1;
}

int exec_editor(struct open_files_thread* oft){
	int wstatus;
	int f = fork();
	if (f < 0){
		fprintf(stderr, "Fork failed\n");
	}
	else if (f == 0){
		exec_editor_fork(oft);
		fprintf(stderr, "Failed to run executable %s\n", *p_exe_path);
		exit(1);
	}
	else{
		printf("Writing file %s from pid %d\n", oft->swp_file_path, f);
		waitpid(f, &wstatus, 0);
		if (WEXITSTATUS(wstatus))
			f = -1;
	}
	return f;
}

static void* thd_open_files(void* arg){
	char path[PATH_MAX + 1];
	struct open_files_thread* oft = (struct open_files_thread*)arg;
	struct oracles o;
	int i, swp_fd;
	swp_fd = pull_swap_file(oft->rf, &o);
	if (swp_fd < 0){
		fprintf(stderr, "Failed to pull file %s\n", oft->rf->file_path);
		goto fail_pcq;
	}
	else if (!get_path_by_fd(path, swp_fd)){ // FUTURE: it would be better if I could move this after the exec
		fprintf(stderr, "Failed to resolve swap file path for %s\n", oft->rf->file_path);
		goto fail_pcq;
	}
	oft->swp_file_path = path;
	pcq_enqueue(&global_qc, oft);
	if (multiple_mode){
		if ((size_t)pcq_dequeue(&global_qp) == 0){
			unlink(path);
		}
		else{
			fail_check(push_swap_file(path, oft->rf, &o) >= 0);
		}
	}
	else{
		do{
			fail_check(exec_editor(oft) >= 0);
			i = push_swap_file(path, oft->rf, &o);
			fail_check(i != -1);
		} while (i < 0);
	}
	goto fail; // really pass
fail_pcq:
	pcq_enqueue(&global_qc, oft);
fail:
	return NULL;
}

struct open_files_thread* open_files(struct range* r){
	int i;
	struct open_files_thread* thds;
	thds = malloc(r->num_files * sizeof(struct open_files_thread)); // free in caller
	if (!thds){
		fprintf(stderr, "Malloc open files threads failed\n");
		return NULL;
	}
	for (i = 0; i < r->num_files; i++){
		thds[i].rf = &r->files[i];
		thds[i].swp_file_path = NULL;
		thds[i].i = i;
		if (pthread_create(&thds[i].thd, NULL, thd_open_files, &thds[i])){
			pcq_enqueue(&global_qc, &thds[i]);
		}
	}
	return thds;
}

int prepare_file_threads(struct range* r){
	struct open_files_thread* thds = NULL, *retval;
	int ret = -1, i, num_thds;
	size_t succ;
	if (pcq_init(&global_qp, r->num_files) < 0 || pcq_init(&global_qc, r->num_files) < 0){
		fprintf(stderr, "Failed to initialize pcqs\n");
		goto fail;
	}
	thds = open_files(r);
	if (thds){
		for (i = num_thds = 0; i < r->num_files; i++){
			retval = (struct open_files_thread*)pcq_dequeue(&global_qc);
			if (retval->swp_file_path){ // thread set up successfully
				num_thds++;
			}
			else{ // thread failed to set up
				zk_unlock_intervals(&r->files[retval->i]);
				thds[retval->i].rf = NULL;
				// pthread_create faiure also ends up here... I'm just going to forget about joining
			}
		}
		fail_check(num_thds > 0);
		if (multiple_mode){
			thds->i = r->num_files; // pass number of elements through i field of first element
			if (exec_editor(thds) < 0){
				succ = 0; // TODO: don't abandon threads?
			}
			else{
				succ = 1;
			}
			for (i = 0; i < num_thds; i++){
				pcq_enqueue(&global_qp, succ);
			}
		}
		for (i = 0; i < r->num_files; i++){
			if (thds[i].rf)
				pthread_join(thds[i].thd, NULL);
		}
	}
	ret = 0;
fail:
	if (thds)
		free(thds);
	pcq_deinit(&global_qp);
	pcq_deinit(&global_qc);
	return ret;
}

int write_offset_update(struct offset_update* ou, int len, int swp_fd, int backing_fd, struct oracles* o){
	int ret = 0, i, tmp_fd;
	off_t pos = 0;
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
		lseek(swp_fd, ou[i].swp_start, SEEK_SET);
		sendfile(tmp_fd, swp_fd, NULL, ou[i].swp_end - ou[i].swp_start);
	}
	sendfile(tmp_fd, backing_fd, NULL, st.st_size - pos);
	fstat(tmp_fd, &st);
	lseek(tmp_fd, 0, SEEK_SET);
	// PONR
	lseek(backing_fd, 0, SEEK_SET);
	fail_check(sendfile(backing_fd, tmp_fd, NULL, st.st_size) >= 0);
	ftruncate(backing_fd, st.st_size);
	fsync(backing_fd);
	goto pass;
fail:
	ret = -1;
pass:
	if (tmp_fd >= 0)
		closec(tmp_fd);
	return ret;
}