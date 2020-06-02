#include <mysql/mysql.h>
#include <stdlib.h>
#include <stdbool.h>
#include "common.h"
#include "sql.h"

// do while (0) to allow semicolon after
#define fail_check(c) \
	do { \
		if (!(c)){ \
			goto fail; \
		} \
	} while (0)
	
#define TXN_START fail_check(!mysql_real_query(mysql, "START TRANSACTION", 17))
#define TXN_COMMIT fail_check(!mysql_real_query(mysql, "COMMIT", 6))
#define TXN_ROLLBACK mysql_real_query(mysql, "ROLLBACK", 8)

// binding, buffer type, buffer, buffer length, length of data in buffer (in) or output (out, non-fixed type), is null, is unsigned, error
#define mysql_bind_init(bind, a, b, c, d, e, f, g) \
	do { \
		(bind).buffer_type = (a); \
		(bind).buffer = (b); \
		(bind).buffer_length = (c); \
		(bind).length = (d); \
		(bind).is_null = (e); \
		(bind).is_signed = (f); \
		(bind).error = (g); \
	} while (0)

static const char* QUERY_SELECT_NAMED_RANGE = "\
	SELECT File.FileId, File.FilePath, Offset.OffsetId, Offset.Base, Offset.Bound, Offset.LBase, Offset.LBound, Offset.Mode, Offset.Conflict \
	FROM \
		((Range INNER JOIN RangeFileJunction ON Range.RangeId = RangeFileJunction.RangeId) \
		INNER JOIN File ON RangeFileJunction.FileId = File.FileId) \
		INNER JOIN Offset ON File.FileId = Offset.FileId \
	WHERE Range.Name = \"?\" AND Range.Init = TRUE\
	ORDER BY File.FilePath, Offset.Base"; // name

static const char* QUERY_SELECT_FILE_INTERVALS[] = {
	"SELECT FileId FROM File WHERE FilePath = \"?\")", // file_path
	"SELECT OffsetId, Base, Bound, LBase, LBound, Mode, Conflict \
	FROM Offset \
	WHERE Offset.FileId = ?" // file_id
}

static const char* QUERY_INSERT_NAMED_RANGE[] = {
	"INSERT INTO Range (Name, init) VALUES (\"?\", FALSE)", // insert new range name
	// mysql_insert_id to get rangeId
	// for each file {
		"INSERT INTO File (FilePath) VALUES (\"?\")", // insert new file path
		// if that failed {
			"SELECT FileId FROM File WHERE FilePath = \"?\"", // SELECT to get fileId
		// }
		// else {
			// mysql_insert_id to get fileId
		// }
		"INSERT INTO RangeFileJunction (RangeId, FileId) VALUES (?, ?)", // rangeId, fileId
		// for each offset {
			"INSERT INTO Offset (FileId, Base, Bound, LBase, LBound, Mode) VALUES (?, ?, ?, ?, ?, ?)", // fileId
		// }
	// }
	// TODO: delay init until other base/bound is captured from files?
	"UPDATE Range SET Range.Init = TRUE WHERE Range.RangeId = ?" // rangeId
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
		"SET @oid = ?, @ob = ?, @nb = ?, @olb = ?, @nlb = ?", // offsetId, old_bound, new_bound, old_lbound, new_lbound
		"SELECT Base, LBase INTO @b, @lb FROM Offset WHERE OffsetId = @oid", // base, lbase
		"UPDATE Offset SET \
			Base = CASE \
				WHEN OffsetId != @oid AND Base >= @ob THEN Base + @nb - @ob \
				ELSE Base END, \
			Bound = CASE \
				WHEN Base <= @b AND Bound >= @ob THEN Bound + @nb - @ob \
				ELSE Bound END, \
			LBase = CASE \
				WHEN OffsetId != @oid AND LBase >= @olb THEN LBase + @nlb - @olb \
				ELSE LBase END, \
			LBound = CASE \
				WHEN LBase <= @lb AND LBound >= @olb THEN LBound + @nlb - @olb \
				ELSE LBound END, \
			Conflict = CASE \
				WHEN OffsetId = @oid THEN FALSE \
				WHEN (@b < Base AND @ob < Bound AND @ob > Base) \
					OR (@b <= Base AND @ob >= Bound) \
					OR (@b > Base AND @b < Bound AND @ob > Bound) THEN TRUE \
				ELSE Conflict END \
		WHERE FileId = ?" // fileId
	// }
};

static MYSQL mysql;

void sql_init(){
	/* mysql_init does this
	if (mysql_library_init(0, NULL, NULL)){
		fprintf(stderr, "Failed to initialize mysql library\n");
	}
	*/
	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql,
		NULL, // localhost // TODO: remote host?
		"root", // user
		"passwd", // password
		NULL, // db name
		0, // port
		"SOCKET_NAME_HERE", // socket
		0 // options
	)){
		fprintf(stderr, "Mysql connection failed\n");
	}
}

void sql_end(){
	mysql_close(&mysql);
	mysql_library_end();
}

int pps(MYSQL_STMT** ret, char* query, MYSQL_BIND* in, MYSQL_BIND* out){ // prepare prepared statement; mysql_stmt_close return value if not NULL
	MYSQL_STMT* stmt;
	fail_check(stmt = mysql_stmt_init(&mysql));
	fail_check(!mysql_stmt_prepare(stmt, QUERY_SELECT_NAMED_RANGE, strlen(QUERY_SELECT_NAMED_RANGE)));
	fail_check(!in || (in && mysql_stmt_bind_param(stmt, &in)));
	fail_check(!out || (out && mysql_stmt_bind_result(stmt, &out)));
	*ret = stmt;
	return 0;
fail:
	return -1;
}

void stmt_errors(MYSQL_STMT* stmt, int n){
	int i;
	for (i = 0; i < n; i++){
		if (mysql_stmt_error(stmt[i])){
			fprintf(stderr, "%s\n", mysql_stmt_error(stmt[i]));
			break;
		}
	}
}

void close_stmts(MYSQL_STMT* stmt, int n){
	int i;
	for (i = 0; i < n; i++)
		if (stmt[i])
			mysql_stmt_close(stmt[i]);
}

int query_select_named_range(struct range* r, char* name){
	#define NUM_STMT 1
	#define NUM_BIND 10
	int ret = 0, i = 0, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	char old_buf[PATH_MAX + 1];
	unsigned long fileId, offsetId;
	size_t base, bound, lbase, lbound;
	unsigned long len;
	char mode, conflict;
	bool null, error;
	
	old_buf[0] = 0;
	len = strlen(name);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, PATH_MAX, &len, &null, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &lbase, sizeof(size_t), NULL, &null, true, &error); // Offset.LBase
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &lbound, sizeof(size_t), NULL, &null, true, &error); // Offset.LBound
	mysql_bind_init(bind[7], MYSQL_TYPE_TINY, &mode, sizeof(char), NULL, &null, true, &error); // Offset.Mode
	mysql_bind_init(bind[8], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	mysql_bind_init(bind[9], MYSQL_TYPE_STRING, name, len, &len, (bool*)0, true, &error); // Range.Name
	
	if (!pps(&stmt[0], QUERY_SELECT_NAMED_RANGE, &bind[9], &bind[0])){
		if (!mysql_stmt_execute(stmt[0])){
			if (!mysql_stmt_store_result(stmt[0])){
				if (!range_init(r, name)){ // no harm if already init'd
					for (;;){
						succ = mysql_stmt_fetch(stmt[0]);
						fail_check(succ == 1);
						if (succ == MYSQL_NO_DATA){
							break;
						}
						if (!strncmp(old_buf, buf, len){ // different file; add it
							memcpy(old_buf, buf, len);
							oldbuf[len + 1] = 0;
							i = range_add_file(r, old_buf, fileId, mode);
							fail_check(!i);
						}
						if (conflict){
							if (mode == RANGE_FILE_MODE_NORMAL){
								printf("Warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
							}
							else{
								printf("Warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", lbase, lbound);
							}
						}
						if (!it_insert(&r->files[i].it, base, bound, lbase, lbound, offsetId)){
							goto fail;
						}
					}
				}
			}
		}
	}
	goto pass;
fail:
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_select_file_intervals(struct range_file* rf, char* file_path){
	#define NUM_STMT 2
	#define NUM_BIND 9
	int ret = 0, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	unsigned long fileId, offsetId;
	size_t base, bound, lbase, lbound;
	unsigned long len;
	char mode, conflict;
	bool null, error;
	
	len = strlen(file_path);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, PATH_MAX, &len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &lbase, sizeof(size_t), NULL, &null, true, &error); // Offset.LBase
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &lbound, sizeof(size_t), NULL, &null, true, &error); // Offset.LBound
	mysql_bind_init(bind[7], MYSQL_TYPE_TINY, &mode, sizeof(char), NULL, &null, true, &error); // Offset.Mode
	mysql_bind_init(bind[8], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	
	fail_check(
		pps(&stmt[0], QUERY_SELECT_NAMED_RANGE, &bind[1], &bind[0]) ||
		pps(&stmt[1], QUERY_SELECT_NAMED_RANGE, &bind[0], &bind[2])
	);
	
	TXN_START;
	
	if (!mysql_stmt_execute(stmt[0])){
		succ = mysql_stmt_fetch(stmt[0]);
		fail_check(succ != 1 && succ != MYSQL_NO_DATA);
		fail_check(rf->file_path = strdup(file_path));
		rf->id = fileId;
		null = false;
		if (!mysql_stmt_execute(stmt[1])){
			if (!mysql_stmt_store_result(stmt[1])){
				for (;;){
					succ = mysql_stmt_fetch(stmt[1]);
					fail_check(succ == 1);
					if (succ == MYSQL_NO_DATA){
						break;
					}
					//if (conflict){
					//	printf("warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
					//}
					rf->mode = mode;
					if (!it_insert(&rf->it, base, bound, lbase, lbound offsetId)){
						goto fail;
					}
				}
			}
		}
	}
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

int query_insert_named_range(struct range* r){
	#define NUM_STMT 6
	#define NUM_BIND 9
	int ret = 0, i, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	struct it_node* p_itn, itn;
	unsigned long rangeId, fileId, name_len;
	char mode;
	bool null, error;
	
	name_len = strlen(r->name);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_STRING, r->name, name_len, &name_len, (bool*)0, true, &error); // Range.Name
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, name_len, &name_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &rangeId, sizeof(size_t), NULL, (bool*)0, true, &error); // RangeId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // FileId
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Base
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.bound, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Bound
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &itn.lbase, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.LBase
	mysql_bind_init(bind[7], MYSQL_TYPE_LONGLONG, &itn.lbound, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.LBound
	mysql_bind_init(bind[8], MYSQL_TYPE_TINY, &mode, sizeof(char), NULL, (bool*)0, true, &error); // Offset.Mode
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(&stmt[0], QUERY_INSERT_NAMED_RANGE[0], &bind[0], NULL) ||
		pps(&stmt[1], QUERY_INSERT_NAMED_RANGE[1], &bind[1], NULL) ||
		pps(&stmt[2], QUERY_INSERT_NAMED_RANGE[2], &bind[2], &bind[3]) ||
		pps(&stmt[3], QUERY_INSERT_NAMED_RANGE[3], &bind[3], NULL) ||
		pps(&stmt[4], QUERY_INSERT_NAMED_RANGE[4], &bind[4], NULL) ||
		pps(&stmt[5], QUERY_INSERT_NAMED_RANGE[5], &bind[3], NULL)
	);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	if (mysql_stmt_affected_rows(stmt[0]) != 1){
		fprintf(stderr, "The range %s already exists\n", r->name);
		goto fail;
	}
	rangeId = mysql_insert_id(&mysql);
	
	for (i = 0; i < r->num_files; i++){
		name_len = strlen(r->files[i].file_path);
		memcpy(buf, r->files[i].file_path, name_len + 1);
		mode = r->files[i].mode;
		fail_check(!mysql_stmt_execute(stmt[1]));
		if (mysql_stmt_affected_rows(stmt[1]) == 0){
			fail_check(!mysql_stmt_execute(stmt[2]));
			succ = mysql_stmt_fetch(stmt[2]);
			fail_check(succ != 1);
			if (succ == MYSQL_NO_DATA){
				fprintf(stderr, "Failed to add file %s\n", buf); // failed to add and failed to receive
				goto fail;
			}
		}
		else {
			fileId = mysql_insert_id(&mysql);
		}
		r->files[i].id = fileId;
		null = false;
		fail_check(!mysql_stmt_execute(stmt[3]));
		it_foreach(&r->files[i].it, p_itn){
			memcpy(&itn, p_itn, sizeof(struct it_node));
			if (!mysql_stmt_execute(stmt[4])){
				if (mode == RANGE_FILE_MODE_NORMAL){
					fprintf(stderr, "Failed to insert interval (%d, %d) in file %s\n", itn.base, itn.bound, buf); // TODO: verbose?
				}
				else{
					fprintf(stderr, "Failed to insert interval (%d, %d) in file %s\n", itn.lbase, itn.lbound, buf); // TODO: verbose?
				}
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

int query_resize_file(struct range_file* rf){
	#define NUM_STMT 4
	#define NUM_BIND 8
	int ret = 0;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	struct it_node* p_itn, itn;
	unsigned long base, lbase;
	int succ;
	bool null, error;
	
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &rf->id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_LONGLONG, &itn.id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &itn.bound, sizeof(size_t), NULL, (bool*)0, true, &error); // old_bound
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // new_bound; overwriting the 'base' field
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &itn.lbound, sizeof(size_t), NULL, (bool*)0, true, &error); // old_lbound
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.lbase, sizeof(size_t), NULL, (bool*)0, true, &error); // new_lbound; overwriting the 'lbase' field
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[7], MYSQL_TYPE_LONGLONG, &lbase, sizeof(size_t), NULL, &null, true, &error); // Offset.LBase
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(&stmt[0], QUERY_RESIZE_FILE[0], &bind[0], NULL) ||
		pps(&stmt[1], QUERY_RESIZE_FILE[1], &bind[1], NULL) ||
		pps(&stmt[2], QUERY_RESIZE_FILE[2], NULL, &bind[6]) ||
		pps(&stmt[3], QUERY_RESIZE_FILE[3], &bind[0], NULL)
	);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	it_foreach(&rf->it, p_itn){
		memcpy(&itn, p_itn, sizeof(struct it_node));
		fail_check(!mysql_stmt_execute(stmt[1]));
		fail_check(!mysql_stmt_execute(stmt[2]));
		succ = mysql_stmt_fetch(stmt[2]); // get updated old bases from DB
		fail_check(succ != 1 && succ != MYSQL_NO_DATA);
		// TODO: updated old bases from DB in base, lbase
		fail_check(!mysql_stmt_execute(stmt[3]));
		p_itn->base = base; // update it_node's field with new base
		p_itn->lbase = lbase; // update it_node's field with new base
	}
	// TODO: write to file at the same time? Should definitely be done w/i transaction
	
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_BIND
	#undef NUM_STMT
}

int thread_init_file(struct range_file* rf){
	#define NUM_STMT 2
	#define NUM_BIND 1
	int ret = 0;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	struct it_node* p_itn, itn;
	unsigned long base, lbase;
	int succ;
	bool null, error;
	
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &rf->id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_LONGLONG, &itn.id, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &itn.bound, sizeof(size_t), NULL, (bool*)0, true, &error); // old_bound
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // new_bound; overwriting the 'base' field
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &itn.lbound, sizeof(size_t), NULL, (bool*)0, true, &error); // old_lbound
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.lbase, sizeof(size_t), NULL, (bool*)0, true, &error); // new_lbound; overwriting the 'lbase' field
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[7], MYSQL_TYPE_LONGLONG, &lbase, sizeof(size_t), NULL, &null, true, &error); // Offset.LBase
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(&stmt[0], QUERY_RESIZE_FILE[0], &bind[0], NULL) ||
		pps(&stmt[1], QUERY_RESIZE_FILE[1], &bind[1], NULL) ||
		pps(&stmt[2], QUERY_RESIZE_FILE[2], NULL, &bind[6]) ||
		pps(&stmt[3], QUERY_RESIZE_FILE[3], &bind[0], NULL)
	);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	it_foreach(&rf->it, p_itn){
		memcpy(&itn, p_itn, sizeof(struct it_node));
		fail_check(!mysql_stmt_execute(stmt[1]));
		fail_check(!mysql_stmt_execute(stmt[2]));
		succ = mysql_stmt_fetch(stmt[2]); // get updated old bases from DB
		fail_check(succ != 1 && succ != MYSQL_NO_DATA);
		// TODO: updated old bases from DB in base, lbase
		fail_check(!mysql_stmt_execute(stmt[3]));
		p_itn->base = base; // update it_node's field with new base
		p_itn->lbase = lbase; // update it_node's field with new base
	}
	// TODO: write to file at the same time? Should definitely be done w/i transaction
	
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_BIND
	#undef NUM_STMT
}
