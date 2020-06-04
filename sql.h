#ifndef SQL_H
#define SQL_H

struct range;
struct range_file;
struct it_node;

int sql_init();
void sql_end();
int query_select_named_range(struct range* r);
int query_select_file_intervals(struct range_file* rf, char* file_path, struct it_node* new_interval);
int query_insert_named_range(struct range* r);
int query_resize_file(struct range_file* f);

/* SCHEMA:
CREATE TABLE Range (
	RangeId SERIAL,
	Name VARCHAR UNIQUE
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
*/

#endif