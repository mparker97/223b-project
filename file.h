#ifndef FILE_H
#define FILE_H
#include <mysql/mysql.h>
#include <pthread.h>
#include "sql.h"
#include "common.h"
#include "range.h"
#include "pcq.h"
#define THD_STATE_OK 0
#define THD_STATE_FAILED 1
#define THD_STATE_TERMINATED 2
#define THD_STATE_RETRY 3

// not const b/c discard qualifier
extern char* DEFAULT_START_ORACLE;
extern char* DEFAULT_END_ORACLE;

extern char swp_dir[PATH_MAX + 1];
extern struct pcq global_qp;
extern struct pcq global_qc;

struct oracles{
	char oracle[2][ORACLE_LEN_MAX + 1];
	size_t oracle_len[2];
};

struct open_files_thread{
	pthread_t thd;
	struct range_file* rf;
	char* swp_file_path;
	int thd_state;
};

struct offset_update{
	size_t backing_start, backing_end, swp_start, swp_end;
};

//int get_path_by_fd(char* path, int fd);
int pull_swap_file(char* swp_path, struct range_file* rf, struct oracles* o);
int push_swap_file(MYSQL* sql, char* swp_path, struct range_file* rf, struct oracles* o);
int exec_editor(struct open_files_thread* oft, int nr);
int prepare_file_threads(struct range* r);
int write_offset_update(struct range_file* rf, struct offset_update* ou, struct oracles* o, int swp_fd);

#endif
