#ifndef SQL_H
#define SQL_H

struct range;
struct range_file;
struct it_node;
typedef struct it_node it_node_t;

int sql_init();
void sql_end();
int query_select_named_range(struct range* r);
int query_select_file_intervals(struct range_file* rf, char* file_path, it_node_t* new_interval);
int query_insert_named_range(struct range* r);
int query_resize_file(struct range_file* f, int swp_fd, int backing_fd);

/* SCHEMA:
CREATE TABLE Range (
	RangeId SERIAL,
	RangeName VARCHAR(64) UNIQUE
);
CREATE TABLE File (
	FileId SERIAL,
	FilePath VARCHAR(4096) UNIQUE
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
	FOREIGN KEY (FileId) REFERENCES File(FileId) ON DELETE CASCADE
);
___________________
CREATE TABLE RangeName (
	RangeId SERIAL,
	Name VARCHAR(64) UNIQUE
);

CREATE TABLE File (
	FileId SERIAL,
	FilePath VARCHAR(3072) UNIQUE
); #exceeded max size

CREATE TABLE RangeFileJunction (
	RangeFileJunctionId SERIAL,
	RangeId bigint UNSIGNED NOT NULL,   #need to match type
  	FileId bigint UNSIGNED NOT NULL,
	FOREIGN Key (RangeId) REFERENCES RangeName(RangeId) ON DELETE CASCADE,
    	FOREIGN Key (FileId) REFERENCES File(FileId) ON DELETE CASCADE
);

CREATE TABLE Offset (
	OffsetId SERIAL,
	Base bigint UNSIGNED NOT NULL,
	Bound bigint UNSIGNED NOT NULL,
	Conflict BOOL DEFAULT FALSE,
	FileId bigint UNSIGNED NOT NULL,
	FOREIGN KEY (FileId) REFERENCES File(FileId) ON DELETE CASCADE
);
*/

#endif
