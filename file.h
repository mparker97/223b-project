#ifndef FILE_H
#define FILE_H
#include <pthread.h>
#include "common.h"
#include "range.h"

// not const b/c discard qualifier
extern char* DEFAULT_START_ORACLE;
extern char* DEFAULT_END_ORACLE;

extern char swp_dir[PATH_MAX + 1];

struct oracles{
	char oracle[2][ORACLE_LEN_MAX + 1];
	size_t oracle_len[2];
};

struct open_files_thread{
	pthread_t thd;
	struct range_file* rf;
};

struct offset_update{
	size_t backing_start, backing_end, swp_start, swp_end;
};

//int get_path_by_fd(char* path, int fd);
int pull_swap_file(struct range_file* rf, struct oracles* o);
int push_swap_file(char* swp_path, struct range_file* rf, struct oracles* o);
struct open_files_thread* open_files(struct range* r);
int write_offset_update(struct offset_update* ou, int len, int swp_fd, int backing_fd, struct oracles* o);

#endif
