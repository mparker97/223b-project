#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "sql.h"
#include "common.h"
#include "interval_tree.h"
#include "range.h"

// do while (0) to allow semicolon after
#define fail_check(c) \
	do { \
		if (!(c)){ \
			goto fail; \
		} \
	} while (0)

#define TXN_START fail_check(!mysql_real_query(&mysql, "START TRANSACTION", 17))
#define TXN_COMMIT fail_check(!mysql_real_query(&mysql, "COMMIT", 6))
#define TXN_ROLLBACK mysql_real_query(&mysql, "ROLLBACK", 8)

// binding, buffer type, buffer, buffer length, length of data in buffer (in) or output (out, non-fixed type), is null, is unsigned, error
#define mysql_bind_init(bind, a, b, c, d, e, f, g) \
	do { \
		(bind).buffer_type = a; \
		(bind).buffer = b; \
		(bind).buffer_length = c; \
		(bind).length = d; \
		(bind).is_null = e; \
		(bind).is_unsigned = f; \
		(bind).error = g; \
	} while (0)

static const char* QUERY_SELECT_NAMED_RANGE = "\
SELECT File.FileId, File.FilePath, Offset.OffsetId, Offset.Base, Offset.Bound, Offset.Conflict \ 
FROM \
(((RangeName INNER JOIN RangeFileJunction ON RangeName.RangeId = RangeFileJunction.RangeId) \
INNER JOIN File ON RangeFileJunction.FileId = File.FileId) \
INNER JOIN Offset ON File.FileId = Offset.FileId) \
	WHERE RangeName.Name = ? AND RangeName.Init = TRUE\
	ORDER BY File.FilePath, Offset.Base LOCK IN SHARE MODE"; // Range.RangeName changed from for share

static const char* QUERY_SELECT_FILE_INTERVALS[] = {
	"SELECT FileId FROM File WHERE FilePath = ?", // file_path
	"SELECT OffsetId, Base, Bound, Conflict \
	FROM Offset \
	WHERE Offset.FileId = ?" // file_id
};

static const char* QUERY_INSERT_NAMED_RANGE[] = {
	"INSERT INTO RangeName (Name, init) VALUES (?, FALSE)", // insert new range name
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
			"INSERT INTO Offset (FileId, Base, Bound) VALUES (?, ?, ?)", // fileId, base, bound, mode
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
		"SET @oid = ?, @nb = ?", // offsetId, new_bound
		"SELECT Base, Bound INTO @b, @ob FROM Offset WHERE OffsetId = @oid", // base, bound
		"UPDATE Offset SET \
			Base = CASE \
				WHEN OffsetId != @oid AND Base >= @ob THEN Base + @nb - @ob \
				ELSE Base END, \
			Bound = CASE \
				WHEN Base <= @b AND Bound >= @ob THEN Bound + @nb - @ob \
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

static MYSQL mysql;

int sql_init(){
	/* mysql_init does this
	if (mysql_library_init(0, NULL, NULL)){
		fprintf(stderr, "Failed to initialize mysql library\n");
	}
	*/
	int ret = 0;
	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql,
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

void sql_end(){
	mysql_close(&mysql);
	mysql_library_end();
}

int pps(MYSQL_STMT** ret, const char* query, MYSQL_BIND* in, MYSQL_BIND* out){ // prepare prepared statement; mysql_stmt_close return value if not NULL
	MYSQL_STMT* stmt;
	fail_check(stmt = mysql_stmt_init(&mysql));
	printf("0s\n");
	fail_check(!mysql_stmt_prepare(stmt, query, strlen(query)));

				//char * p = mysql_stmt_error(stmt);
				//printf("\n%s\n",p);
	*ret = stmt;
	printf("1\n");
	if(in!=NULL){
		mysql_stmt_bind_param(stmt, in);
	printf("2\n");
	}
	if(out!=NULL){
		mysql_stmt_bind_result(stmt, out);
	printf("3\n");
	}
	return 1;
fail:
	return 0;
}

void stmt_errors(MYSQL_STMT** stmt, int n){
	int i;
	for (i = 0; i < n; i++){
		const char* e = mysql_stmt_error(stmt[i]);
		if (e){
			fprintf(stderr, "%s\n", e);
			break;
		}
	}
}

void close_stmts(MYSQL_STMT** stmt, int n){
	int i;
	for (i = 0; i < n; i++)
		if (stmt[i])
			mysql_stmt_close(stmt[i]);
}

int query_select_named_range(struct range* r){ // range already has r->name
	#define NUM_STMT 1
	#define NUM_BIND 7
	int ret = 0, succ;
	struct range_file* rf = NULL;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	char old_buf[PATH_MAX + 1];
	unsigned long fileId, offsetId;
	size_t base, bound;
	unsigned long len;
	char conflict;
	char null, error;
	MYSQL_RES * res;
	
	old_buf[0] = 0;
	len = strlen(r->name);
	int path_len = PATH_MAX;
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	memset(buf, 0, PATH_MAX+1);
	memset(old_buf, 0, PATH_MAX+1);
	
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, path_len, &path_len, (bool*)0, true, &error); // File.FilePath
	
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[5], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	mysql_bind_init(bind[6], MYSQL_TYPE_STRING, r->name, len, &len, (bool*)0, true, &error); // Range.Name
	
	fail_check(
		pps(&stmt[0], QUERY_SELECT_NAMED_RANGE, &bind[6], &bind[0])
	);
	TXN_START;
	fail_check(!mysql_stmt_execute(stmt[0]));
	res = mysql_stmt_store_result(stmt[0]);
	//printf("res is %d\n",res); res is always 0
	mysql_free_result(res);
	for (;;){
		succ = mysql_stmt_fetch(stmt[0]);
		fail_check(succ != 1);

		if (succ == MYSQL_NO_DATA){
			break;
		}

		printf("id: %d\n",offsetId);
		printf("path: %s\n",buf);
		printf("fileid: %d\n",fileId);
		printf("base: %d\n",base);
		printf("bound: %d\n",bound);
		printf("conflict: %d\n",conflict);
	/*	if (!strncmp(old_buf, buf, len)){ // different file; add it
			memcpy(old_buf, buf, len);
			old_buf[len + 1] = 0;
			rf = range_add_file(r, old_buf, fileId);
			fail_check(rf);
		}*/
		if (conflict){
			printf("Warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
		}
		//range_file_add_it(rf, base, bound, offsetId); // no failure; just don't include it
	}
	/*TODO: ZK LOCK
	if (successful){
		open_files(r);
	}
	else {
		fprintf(stderr, "Range %s is already in use\n", r->name);
		goto fail;
	}
	
	TXN_COMMIT;*/
	goto pass;
fail:
	TXN_ROLLBACK;
	printf("failed\n");
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	//close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_select_file_intervals(struct range_file* rf, char* file_path, it_node_t* new_interval){
	#define NUM_STMT 2
	#define NUM_BIND 6
	int ret = 0, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	unsigned long fileId, offsetId;
	size_t base, bound;
	unsigned long len;
	char conflict;
	char null, error;
	char buf[PATH_MAX + 1];
	MYSQL_RES * res;
	memset(buf, 0, PATH_MAX+1);
	//struct l_list *cur_ls = &rf->it;
	if (strlen(file_path)<PATH_MAX){
		for (int i=0;i<strlen(file_path);i++){
			buf[i]=file_path[i];
		}	
	}
	int path_len = PATH_MAX;
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, path_len, &path_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, &null, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, &null, true, &error); // Offset.Base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bound, sizeof(size_t), NULL, &null, true, &error); // Offset.Bound
	mysql_bind_init(bind[5], MYSQL_TYPE_TINY, &conflict, sizeof(char), NULL, &null, true, &error); // Offset.Conflict
	
	fail_check(
		pps(&stmt[0], QUERY_SELECT_FILE_INTERVALS[0], &bind[1], &bind[0]) &&
		pps(&stmt[1], QUERY_SELECT_FILE_INTERVALS[1], &bind[0], &bind[2])
	);
	
	rf->file_path = NULL;
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	res = mysql_stmt_store_result(stmt[0]);
	//printf("%d\n",res); res is always 0
	mysql_free_result(res);
	
	succ = mysql_stmt_fetch(stmt[0]);
	fail_check(succ != 1 && succ != MYSQL_NO_DATA);
	
	fail_check(rf->file_path = strdup(file_path));
	
	rf->id = fileId;
	null = false;
	printf("fileid: %d\n",rf->id);
	
	fail_check(!mysql_stmt_execute(stmt[1]));
	
	res = mysql_stmt_store_result(stmt[1]);
	//printf("%d\n",res);
	mysql_free_result(res);
	
	for (;;){
		succ = mysql_stmt_fetch(stmt[1]);
		fail_check(succ != 1);
		if (succ == MYSQL_NO_DATA){
			break;
		}
		if (conflict){
			printf("warning: Interval [%lu, %lu) has been modified and might be inaccurate\n", base, bound);
		}
		printf("offsetid: %d\n",offsetId);
		printf("base: %d\n",base);
		printf("bound: %d\n",bound);
		printf("conflict: %d\n",conflict);
		it_node_t* cur_interval = (it_node_t*) malloc(sizeof(it_node_t));
		fail_check(cur_interval != NULL);
		cur_interval->ls = L_LIST_NULL;
		cur_interval->base = base;
		cur_interval->bound = bound;
		cur_interval->id = offsetId;

		//if (it_intersect(new_interval, cur_interval)) {
		//	l_list_add_after(cur_ls, &(cur_interval->ls));
	//		cur_ls = &(cur_interval->ls);
	//	}
	//	else {
	//		free(cur_interval);
	//	}
	}
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	printf("failed\n");
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

int query_insert_named_range(struct range* r){
	#define NUM_STMT 6
	#define NUM_BIND 6
	int ret = 0, i, succ;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	struct it_node* p_itn, itn;
	unsigned long rangeId, fileId, name_len;
	char null, error;
	name_len = strlen(r->name);
	int path_len = PATH_MAX;
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	memset(buf, 0, PATH_MAX+1);
	MYSQL_RES * res;
	
	
	bool temp = false;	
	mysql_bind_init(bind[0], MYSQL_TYPE_STRING, r->name, name_len, &name_len, &temp, true, &error); // Range.Name
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, buf, path_len, &path_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &rangeId, sizeof(size_t), NULL, (bool*)0, true, &error); // RangeId
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, &null, true, &error); // FileId
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Base
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.bound, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Bound
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	
	printf("buffer: %s\n",bind[0].buffer);
	//MYSQL_STMT *ppstmt;
	//ppstmt = mysql_stmt_init(&mysql);
	//mysql_stmt_prepare(ppstmt, QUERY_INSERT_NAMED_RANGE[0], strlen(QUERY_INSERT_NAMED_RANGE[0]));
	//mysql_stmt_bind_param(ppstmt, &bind[0]);
	
	//mysql_stmt_bind_result(ppstmt, &bind[0]);
	//fail_check(!mysql_stmt_prepare(stmt, query, strlen(query)));
	//fail_check(in==NULL || mysql_stmt_bind_param(stmt, in));
	//fail_check(out==NULL || mysql_stmt_bind_result(stmt, out));
	//*ret = stmt;
	fail_check(
		pps(&stmt[0], QUERY_INSERT_NAMED_RANGE[0], &bind[0], NULL)&&
		pps(&stmt[1], QUERY_INSERT_NAMED_RANGE[1], &bind[1], NULL)&&
		pps(&stmt[2], QUERY_INSERT_NAMED_RANGE[2], &bind[1], &bind[3])&&
		pps(&stmt[3], QUERY_INSERT_NAMED_RANGE[3], &bind[2], NULL)&&
		pps(&stmt[4], QUERY_INSERT_NAMED_RANGE[4], &bind[3], NULL)&&
		pps(&stmt[5], QUERY_INSERT_NAMED_RANGE[5], &bind[2], NULL)
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
		fail_check(!mysql_stmt_execute(stmt[1]));
		if (mysql_stmt_affected_rows(stmt[1]) == 0){
			fail_check(!mysql_stmt_execute(stmt[2]));
			res = mysql_stmt_store_result(stmt[2]);
			//printf("res is %d\n",res); res is always 0
			mysql_free_result(res);
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
			
			//printf("\nbase: %d",itn.base);
			//printf("\nbound: %d",itn.bound);
			memcpy(&itn, p_itn, sizeof(struct it_node));
			//printf("\nbase: %d",itn.base);
			//printf("\nbound: %d",itn.bound);
			if (mysql_stmt_execute(stmt[4])){
				//char * p = mysql_stmt_error(stmt[4]);
				//printf("\n%s\n",p);
				fprintf(stderr, "Failed to insert interval (%lu, %lu) in file %s\n", itn.base, itn.bound, buf);
			}
		}
	}
	
	fail_check(!mysql_stmt_execute(stmt[5]));
	TXN_COMMIT;
	
	goto pass;
	

fail:
	TXN_ROLLBACK;
	printf("failed&");
	ret = -1;
	stmt_errors(stmt, NUM_STMT);
pass:
	close_stmts(stmt, NUM_STMT);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}