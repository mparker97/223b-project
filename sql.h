#ifndef SQL_H
#define SQL_H
#include <mysql/mysql.h>

struct range;
struct range_file;
struct it_node;
struct oracles;
typedef struct it_node it_node_t;
extern MYSQL global_mysql;

int sql_init(MYSQL* mysql);
void sql_deinit(MYSQL* mysql, int is_thread);
int query_select_named_range(MYSQL* mysql, struct range* r, char** files, int lock);
int query_select_file_intervals(MYSQL* mysql, struct range_file* rf, char* file_path, unsigned long cur_id);
int query_insert_named_range(MYSQL* mysql, struct range* r);
int query_resize_file(MYSQL* mysql, struct range_file* f, struct oracles* o, int swp_fd);
#include "file.h"

/* SCHEMA:
CREATE TABLE RangeName (
	RangeId SERIAL,
	Name VARCHAR(64) UNIQUE,
	Init BOOL DEFAULT FALSE
);
CREATE TABLE File (
	FileId SERIAL,
	FilePath VARCHAR(3072) UNIQUE
);
CREATE TABLE RangeFileJunction (
	RangeFileJunctionId SERIAL,
	RangeId LONG NOT NULL,
	FileId LONG NOT NULL,
	FOREIGN KEY (RangeId) REFERENCES Range(RangeId) ON DELETE CASCADE,
	FOREIGN KEY (FileId) REFERENCES File(FileId) ON DELETE CASCADE
);
CREATE TABLE Offset (
	OffsetId LONG SERIAL,
	Base LONG NOT NULL,
	Bound LONG NOT NULL,
	Conflict BOOL DEFAULT FALSE,
	FileId LONG NOT NULL,
	RangeId LONG NOT NULL,
	FOREIGN KEY (FileId) REFERENCES File(FileId) ON DELETE CASCADE
	FOREIGN KEY (RangeId) REFERENCES RangeName(RangeId) ON DELETE CASCADE
);
*/

#endif
