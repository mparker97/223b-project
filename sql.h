#ifndef SQL_H
#define SQL_H
#include "range.h"

int query_select_named_range(struct range* r, char* name);
int query_select_file_intervals(struct range_file* rf, char* file_path);
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
	LBase LONG NOT NULL,
	LBound LONG NOT NULL,
	Mode CHAR NOT NULL,
	Conflict BOOL DEFAULT FALSE,
	FileId LONG NOT NULL,
	FOREIGN KEY (FileId) REFERENCES File(FileId) ON DELETE CASCADE
);
*/

#endif