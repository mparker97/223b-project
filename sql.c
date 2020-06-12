#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "file.h"
#include "sql.h"
#include "common.h"
#include "interval_tree.h"
#include "range.h"
#include "zkclient.h"
#include "pcq.h"

#define TXN_START fail_check(!mysql_real_query(mysql, "START TRANSACTION", 17))
#define TXN_COMMIT fail_check(!mysql_real_query(mysql, "COMMIT", 6))
#define TXN_ROLLBACK mysql_real_query(mysql, "ROLLBACK", 8)

// binding, buffer type, buffer, buffer length, length of data in buffer (in) or output (out, non-fixed type), is null, is unsigned, error
#define mysql_bind_init(bind, a, b, c, d, e, f, g) \
	do { \
		(bind).buffer_type = a; \
		(bind).buffer = b; \
		(bind).buffer_length = c; \
		(bind).length = d; \
		WIGNORE(-Wincompatible-pointer-types, (bind).is_null = e); \
		(bind).is_unsigned = f; \
		(bind).error = g; \
	} while (0)

static const char* QUERY_SELECT_NAMED_RANGE = "\
	SELECT File.FileId, File.FilePath, Offset.OffsetId, Offset.Base, Offset.Bound, Offset.Conflict \
	FROM \
	(((RangeName INNER JOIN RangeFileJunction ON RangeName.RangeId = RangeFileJunction.RangeId) \
		INNER JOIN File ON RangeFileJunction.FileId = File.FileId) \
		INNER JOIN Offset ON File.FileId = Offset.FileId) \
	WHERE RangeName.RangeId = Offset.RangeId AND RangeName.Name = ? AND RangeName.Init = TRUE\
	ORDER BY File.FilePath, Offset.Base LOCK IN SHARE MODE"; // Range.RangeName changed from for share

static const char* QUERY_SELECT_FILE_INTERVALS[] = {
	"SELECT FileId FROM File WHERE FilePath = ?", // file_path
	"SELECT Base, Bound FROM Offset WHERE OffsetId = ? LOCK IN SHARE MODE", // base, bound; cur_id
	"SELECT Base, Bound, OffsetId, Conflict FROM Offset WHERE Offset.FileId = ? LOCK IN SHARE MODE" // file_id
};

static const char* QUERY_INSERT_NAMED_RANGE[] = {
	"INSERT INTO RangeName (Name, Init) VALUES (?, FALSE)", // insert new range name
	// mysql_insert_id to get rangeId
	// for each file {
		"INSERT INTO File (FilePath) VALUES (?)", // insert new file path
		// if that failed {
			"SELECT FileId FROM File WHERE FilePath = ?", // SELECT to get fileId
		// }
		// else {
			// mysql_insert_id to get fileId
		// }
		"INSERT INTO RangeFileJunction (RangeId, FileId) VALUES (?, ?)", // rangeId, fileId
		// for each offset {
			"INSERT INTO Offset (RangeId, FileId, Base, Bound) VALUES (?, ?, ?, ?)", // fileId, base, bound, mode
		// }
	// }
	"UPDATE RangeName SET RangeName.Init = TRUE WHERE RangeName.RangeId = ?" // rangeId
};

static const char* QUERY_RESIZE_FILE[] = {
	/*
		One transaction per file
		When range compenent 0 (A,B) is updated through edit,
			A remains and B can be changed to anywhere from 1 after A to the end of the file.
		How the Base, Bound, and Conflict for separate component 1 (C,D) are affected depends on the case.
		If there is a conflict, component 1 will be changed to maximize its reach,
			allowing users the ability to correct this manually on the next edit
		Case 0: Base1 >= Bound0
			Simplified for If: Base1 >= Bound0
		.......A-------B.......
		.................C---D.
		Base1 += change
		
		Case 1: Base1 >= Base0 AND Base1 < Bound0 AND Bound1 > Bound0
			Simplified for If: Base1 >= Base0 AND Bound1 > Bound0
		.......A-------B.......
		............C----D.....
		Conflict = True
		Base1 = Base0
		Bound1 += MAX(0, change)
		
		Case 2: Base1 >= Base0 AND Bound1 <= Bound0
			Simplified for If: Base1 >= Base0
		.......A-------B.......
		.........C---D.........
		Conflict = True
		Base1 = Base0
		Bound1 = Bound0 // BAD!!!
		
		Case 3: Base1 < Base0 AND Bound1 > Bound0
			Simplified for If: Bound1 > Bound0
		.......A-------B.......
		......C---------D......
		Bound1 += change
		
		Case 4: Base1 < Base0 AND Bound1 > Base0 AND Bound1 <= Bound0
			Simplified for If: Bound1 > Base0
		.......A-------B.......
		.....C----D............
		Conflict = True
		Bound1 += MAX(0, change)
		
		Case 5: Bound1 < Base0
			Simplified for If: Else
		.......A-------B.......
		.C---D.................
		(Do nothing)
	*/
	
	"SELECT OffsetId FROM Offset WHERE FileId = ? FOR UPDATE", // lock all of file's offsets: fileId; 
	// for each offset in this file for the range {
		"SET @oid = ?, @ns = ?", // offsetId, new_size
		"SELECT Base, Bound INTO @b, @ob FROM Offset WHERE OffsetId = @oid", // base, bound
		"SELECT Base, Bound FROM Offset WHERE OffsetId = @oid", // base, bound
		"UPDATE Offset SET \
			Base = CASE \
				WHEN OffsetId != @oid AND Base >= @ob THEN Base + @ns - (@ob - @b) \
				ELSE Base END, \
			Bound = CASE \
				WHEN Bound >= @ob THEN Bound + @ns - (@ob - @b) \
				ELSE Bound END, \
			Conflict = CASE \
				WHEN OffsetId = @oid THEN FALSE \
				WHEN (@b < Base AND @ob < Bound AND @ob > Base) \
					OR (@b <= Base AND @ob >= Bound) \
					OR (@b > Base AND @b < Bound AND @ob > Bound) THEN TRUE \
				ELSE Conflict END \
		WHERE FileId = ?" // fileId
	// }
};

MYSQL global_mysql;

int sql_init(MYSQL* mysql){
	int ret = 0;
	mysql_init(mysql);
	if (!mysql_real_connect(mysql,
		"172.31.24.95", // localhost // TODO: remote host?
		"client", // user
		"password", // password
		"test", // db name
		0, // port
		NULL, // socket
		0 // options
	)){
		fprintf(stderr, "Mysql connection failed\n");
		ret = -1;
	}
	return ret;
}

inline void sql_deinit(MYSQL* mysql, int is_thread){
	if (is_thread)
		mysql_thread_end();
	mysql_close(mysql);
}

static bool pps_len(MYSQL_STMT** ret, MYSQL* mysql, const char* query, MYSQL_BIND* in, MYSQL_BIND* out, size_t len){ // prepare prepared statement; mysql_stmt_close return value if not NULL
	MYSQL_STMT* stmt;
	fail_check(stmt = mysql_stmt_init(mysql));
	fail_check(!mysql_stmt_prepare(stmt, query, len));
	*ret = stmt;
	if (in){
		fail_check(!mysql_stmt_bind_param(stmt, in));
	}
	if (out){
		fail_check(!mysql_stmt_bind_result(stmt, out));
	}
	return true;
fail:
	return false;
}

#define pps(a, b, c, d, e) pps_len(a, b, c, d, e, strlen(c))
#define pps_nolock(a, b, c, d, e) pps_len(a, b, c, d, e, strlen(c) - 19)

static void stmt_errors(MYSQL_STMT** stmt, int n){
	int i;
	for (i = 0; i < n; i++){
		const char* e = mysql_stmt_error(stmt[i]);
		if (e){
			fprintf(stderr, "%s\n", e);
			break;
		}
	}
}

static void close_stmts(MYSQL_STMT** stmt, int n){
	int i;
	for (i = 0; i < n; i++)
		if (stmt[i])
			mysql_stmt_close(stmt[i]);
}

int query_select_named_range(MYSQL* mysql, struct range* r, char** files, int lock){ // range already has r->name
	#define NUM_STMT 1
	#define NUM_BIND 7
	int ret = 0, i, succ, committed = 0;
	struct range_file* rf = NULL;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	char old_buf[PATH_MAX + 1];
	unsigned long fileId, offsetId;
	size_t base, bound;
	unsigned long len;
	unsigned long path_len;
	char conflict;
	char null = false, error;
	
	old_buf[0] = 0;
	len = strlen(r->name);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, PATH_MAX+1, &path_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[5], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	mysql_bind_init(bind[6], MYSQL_TYPE_STRING, r->name, len, &len, (bool*)0, true, &error); // Range.Name
	
	TXN_START;
	if (lock)
		fail_check(pps(&stmt[0], mysql, QUERY_SELECT_NAMED_RANGE, &bind[6], &bind[0]));
	else
		fail_check(pps_nolock(&stmt[0], mysql, QUERY_SELECT_NAMED_RANGE, &bind[6], &bind[0])); // take off lock
	fail_check(!mysql_stmt_execute(stmt[0]));
	fail_check(!mysql_stmt_store_result(stmt[0]));
	for (;;){
		succ = mysql_stmt_fetch(stmt[0]);
		fail_check(succ != 1);
		if (succ == MYSQL_NO_DATA){
			break;
		}
		buf[path_len] = 0;
		if (strcmp(old_buf, buf)){ // different file; add it
			if (*files){
				for (i = 0; files[i] && strcmp(files[i], buf); i++);
				if (!files[i]){
					continue;
				}
			}
			memcpy(old_buf, buf, path_len);
			old_buf[path_len] = 0;
			rf = range_add_file(r, old_buf, fileId);
			fail_check(rf);
		}
		if (conflict){
			printf("Warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
		}
		range_file_add_it(rf, base, bound, offsetId); // no failure check; just don't include it if it fails
	}
	
	// iterate through all range files and attempt acquiring interval locks
	if (lock){
		for (i = 0; i < r->num_files; i++){
			if (zk_lock_intervals(mysql, &r->files[i]) < r->files[i].num_it){
				fprintf(stderr, "Range %s is already in use\n", r->name);
				goto fail_unlock;
			}
		}
		TXN_COMMIT;
		if (prepare_file_threads(r) < 0){
			committed = 1;
			goto fail_unlock;
		}
		goto pass;
	}
	
	TXN_COMMIT;
	goto pass;
fail_unlock:
	// retry delete all possibly acquired interval locks
	//for (i = 0; i < r->num_files; i++){
	//	zk_unlock_intervals(&r->files[i]);
	//}
fail:
	if (!committed)
		TXN_ROLLBACK;
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_select_file_intervals(MYSQL* mysql, struct range_file* rf, char* file_path, unsigned long cur_id){
	#define NUM_STMT 3
	#define NUM_BIND 6
	int ret = 0, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	it_node_t* cur_interval;
	unsigned long fileId, offsetId;
	size_t base, bound;
	char conflict;
	char null = false, error;
	struct l_list *cur_ls = &rf->it;
	it_node_t new_interval;
	unsigned long path_len = strlen(file_path);

	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, file_path, path_len+1, &path_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[5], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	
	fail_check(pps(&stmt[0], mysql, QUERY_SELECT_FILE_INTERVALS[0], &bind[1], &bind[0]));
	if (cur_id == ID_NONE){
		fail_check(
			pps_nolock(&stmt[1], mysql, QUERY_SELECT_FILE_INTERVALS[1], &bind[4], &bind[2]) &&
			pps_nolock(&stmt[2], mysql, QUERY_SELECT_FILE_INTERVALS[2], &bind[0], &bind[2])
		);
	}
	else{
		fail_check(
			pps(&stmt[1], mysql, QUERY_SELECT_FILE_INTERVALS[1], &bind[4], &bind[2]) &&
			pps(&stmt[2], mysql, QUERY_SELECT_FILE_INTERVALS[2], &bind[0], &bind[2])
		);
	}
	rf->file_path = NULL;
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	fail_check(!mysql_stmt_store_result(stmt[0]));
	succ = mysql_stmt_fetch(stmt[0]);
	fail_check(succ != 1 && succ != MYSQL_NO_DATA);
	fail_check(rf->file_path = strdup(file_path));
	rf->id = fileId;
	
	offsetId = cur_id;
	fail_check(!mysql_stmt_execute(stmt[1]));
	fail_check(!mysql_stmt_store_result(stmt[1]));
	succ = mysql_stmt_fetch(stmt[1]);
	if (cur_id != ID_NONE) {
		fail_check(succ != 1 && succ != MYSQL_NO_DATA);

		// get base and bound of offset cur_id
		new_interval.base = base;
		new_interval.bound = bound;
	}	
	
	fail_check(!mysql_stmt_execute(stmt[2]));
	fail_check(!mysql_stmt_store_result(stmt[2]));
	for (;;){
		succ = mysql_stmt_fetch(stmt[2]);
		fail_check(succ != 1);
		if (succ == MYSQL_NO_DATA){
			break;
		}
		if (conflict){
			printf("warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
		}

		if (cur_id == ID_NONE) { // used for printing
			fail_check(it_insert(&rf->it, base, bound, offsetId));
		}
		else { // used for interval locks
			cur_interval = calloc(1, sizeof(it_node_t));
			fail_check(cur_interval != NULL);
			cur_interval->ls = L_LIST_NULL;
			cur_interval->base = base;
			cur_interval->bound = bound;
			cur_interval->id = offsetId;
			if (it_conflict(&new_interval, cur_interval)) {
				l_list_add_after(cur_ls, &(cur_interval->ls));
				cur_ls = &(cur_interval->ls);
			}
			else {
				free(cur_interval);
			}
		}
	}
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	if (rf->file_path)
		free(rf->file_path);
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_insert_named_range(MYSQL* mysql, struct range* r){
	#define NUM_STMT 6
	#define NUM_BIND 6
	int ret = 0, i, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	struct it_node* p_itn, itn;
	unsigned long rangeId, fileId, name_len;
	char null = false, error;
	
	name_len = strlen(r->name);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_STRING, r->name, name_len, &name_len, (bool*)0, true, &error); // Range.Name
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, name_len, &name_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &rangeId, sizeof(size_t), NULL, (bool*)0, true, &error); // RangeId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // FileId
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Base
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.bound, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Bound
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(&stmt[0], mysql, QUERY_INSERT_NAMED_RANGE[0], &bind[0], NULL) &&
		pps(&stmt[1], mysql, QUERY_INSERT_NAMED_RANGE[1], &bind[1], NULL) &&
		pps(&stmt[2], mysql, QUERY_INSERT_NAMED_RANGE[2], &bind[1], &bind[3]) &&
		pps(&stmt[3], mysql, QUERY_INSERT_NAMED_RANGE[3], &bind[2], NULL) &&
		pps(&stmt[4], mysql, QUERY_INSERT_NAMED_RANGE[4], &bind[2], NULL) &&
		pps(&stmt[5], mysql, QUERY_INSERT_NAMED_RANGE[5], &bind[2], NULL)
	);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	if (mysql_stmt_affected_rows(stmt[0]) != 1){
		fprintf(stderr, "The range %s already exists\n", r->name);
		goto fail;
	}
	rangeId = mysql_insert_id(mysql);
	
	for (i = 0; i < r->num_files; i++){
		name_len = strlen(r->files[i].file_path);
		memcpy(buf, r->files[i].file_path, name_len + 1);
		mysql_stmt_execute(stmt[1]);
		if (mysql_stmt_affected_rows(stmt[1]) == (u_int64_t)-1){ // file already exists; grab its id
			fail_check(!mysql_stmt_execute(stmt[2]));
			fail_check(!mysql_stmt_store_result(stmt[2]));
			for (;;){
				succ = mysql_stmt_fetch(stmt[2]);
				fprintf(stderr,"\nsucc is %d\n",succ);
				fail_check(succ != 1);
				if (succ == MYSQL_NO_DATA){
					break;
				}
			}
		}
		else {
			fileId = mysql_insert_id(mysql); // file did not exist and was thus added; retrieve id
		}
		r->files[i].id = fileId;
		fail_check(!mysql_stmt_execute(stmt[3]));
		it_foreach(&r->files[i].it, p_itn){
			memcpy(&itn, p_itn, sizeof(struct it_node));
			if (mysql_stmt_execute(stmt[4])){
				fprintf(stderr, "Failed to insert interval (%lu, %lu) in file %s\n", itn.base, itn.bound, buf);
			}
		}
	}
	fail_check(!mysql_stmt_execute(stmt[5]));
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_resize_file(MYSQL* mysql, struct range_file* rf, struct oracles* o, int swp_fd){
	#define NUM_STMT 5
	#define NUM_BIND 5
	int ret = 0, i = 0, unlock = 0, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	struct it_node* p_itn;
	struct offset_update* ou = NULL;
	unsigned long id, new_sz, db_base, db_bound;
	long delta = 0;
	char null = false, error;
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &rf->id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_LONGLONG, &id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &new_sz, sizeof(size_t), NULL, (bool*)0, true, &error); // new_bound
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &db_base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &db_bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(&stmt[0], mysql, QUERY_RESIZE_FILE[0], &bind[0], NULL) &&
		pps(&stmt[1], mysql, QUERY_RESIZE_FILE[1], &bind[1], NULL) &&
		pps(&stmt[2], mysql, QUERY_RESIZE_FILE[2], NULL, NULL) &&
		pps(&stmt[3], mysql, QUERY_RESIZE_FILE[3], NULL, &bind[3]) &&
		pps(&stmt[4], mysql, QUERY_RESIZE_FILE[4], &bind[0], NULL)
	);
	ou = malloc(rf->num_it * sizeof(struct offset_update));
	fail_check(ou);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	zk_unlock_intervals(rf);
	unlock = 1;
	fail_check(!mysql_stmt_store_result(stmt[0]));
	for(;;){
		succ = mysql_stmt_fetch(stmt[0]); // get updated old base and bound from DB
		fail_check(succ != 1);
		if(succ == MYSQL_NO_DATA){
			break;
		}
	}
	it_foreach(&rf->it, p_itn){
		id = p_itn->id;
		new_sz = p_itn->bound - p_itn->base;
		fail_check(!mysql_stmt_execute(stmt[1]));
		fail_check(!mysql_stmt_execute(stmt[2]));
		fail_check(!mysql_stmt_execute(stmt[3]));
		fail_check(!mysql_stmt_store_result(stmt[3]));
		do{
			succ = mysql_stmt_fetch(stmt[3]); // get updated old base and bound from DB
			fail_check(succ != 1);
		} while (succ != MYSQL_NO_DATA);
		fail_check(!mysql_stmt_execute(stmt[4]));
		ou[i].backing_start = db_base - delta;
		ou[i].backing_end = db_bound - delta;
		ou[i].swp_start = p_itn->base;
		ou[i].swp_end = p_itn->bound;
		delta += new_sz;
		delta -= (db_bound - db_base);
		i++;
	}
	fail_check(write_offset_update(rf, ou, o, swp_fd) >= 0);
	
	TXN_COMMIT;
	goto pass;
fail:
	if (!unlock)
		zk_unlock_intervals(rf);
	TXN_ROLLBACK;
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	if (ou)
		free(ou);
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_BIND
	#undef NUM_STMT
}
