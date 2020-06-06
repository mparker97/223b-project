#ifndef FILE_H
#define FILE_H
#include "common.h"
#include "range.h"

extern char swp_dir[PATH_MAX];
extern char start_oracle[ORACLE_LEN_MAX+1];
extern char end_oracle[ORACLE_LEN_MAX+1];

struct offset_update{
	size_t backing_start, backing_end, swp_start, swp_end;
};

void get_path_by_fd(char* path, int fd);
int pull_swap_file(struct range_file* rf);
int push_swap_file(int swp_fd, struct range_file* rf);
void open_files(struct range* r);
int write_offset_update(struct offset_update* ou, int len, int swp_fd, int backing_fd);

#endif