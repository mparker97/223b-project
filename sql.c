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

const char* QUERY_SELECT_NAMED_RANGE = "\
	SELECT File.FileId, File.FilePath, Offset.OffsetId, Offset.Base, Offset.Size \
	FROM \
		((Range INNER JOIN RangeFileJunction ON Range.RangeId = RangeFileJunction.RangeId) \
		INNER JOIN File ON RangeFileJunction.FileId = File.FileId) \
		INNER JOIN Offset ON File.FileId = Offset.FileId \
	WHERE Range.Name = \"?\" AND Range.Init = TRUE\
	ORDER BY File.FilePath, Offset.Base";

const char* QUERY_INSERT_NAMED_RANGE[] = {
	"INSERT INTO Range (Name, User, init) VALUES (\"?\", ?, FALSE)", // insert new range name
	// mysql_insert_id to get rangeId
	// for each file {
		"INSERT INTO File (FilePath) VALUES (\"?\")", // insert new file path
		// if that failed {
			"SELECT FileId FROM File WHERE FilePath = \"?\"", // SELECT to get fileId
		// }
		// else {
			// mysql_insert_id to get fileId
		// }
		"INSERT INTO RangeFileJunction (RangeId, FileId) VALUES (?, ?)", // use rangeId, fileId
		// for each offset {
			"INSERT INTO Offset (FileId, Base, Size) VALUES (?, ?, ?)", // use fileId
		// }
	// }
	"UPDATE Range SET Range.init = TRUE WHERE Range.RangeId = ?" // use rangeId
};

const char* QUERY_RESIZE_FILE[] = {
	/*
		One transaction per file
		When range compenent 0 (A,B) is updated through edit,
			A remains and B can be changed to anywhere from 1 after A to the end of the file.
		How the Base, Size, and Conflict for separate component 1 (C,D) are affected depends on the case.
		If there is a conflict, component 1 will be changed to maximize its reach,
			allowing users the ability to correct this manually on the next edit
		Case 0: Base1 >= Base0 + Size0
			Simplified for If: Base1 >= Base0 + Size0
		.......A-------B.......
		.................C---D.
		Base1 += change
		
		Case 1: Base1 >= Base0 AND Base1 < Base0 + Size0 AND Base1 + Size1 > Base0 + Size0
			Simplified for If: Base1 >= Base0 AND Base1 + Size1 > Base0 + Size0
		.......A-------B.......
		............C----D.....
		Conflict = True
		Base1 = Base0
		Size1 += MAX(0, change)
		
		Case 2: Base1 >= Base0 AND Base1 + Size1 <= Base0 + Size0
			Simplified for If: Base1 >= Base0
		.......A-------B.......
		.........C---D.........
		Conflict = True
		Base1 = Base0
		Size1 = Size0 // BAD!!!
		
		Case 3: Base1 < Base0 AND Base1 + Size1 > Base0 + Size0
			Simplified for If: Base1 + Size1 > Base0 + Size0
		.......A-------B.......
		......C---------D......
		Size1 += change
		
		Case 4: Base1 < Base0 AND Base1 + Size1 > Base0 AND Base1 + Size1 <= Base0 + Size0
			Simplified for If: Base1 + Size1 > Base0
		.......A-------B.......
		.....C----D............
		Conflict = True
		Size1 += MAX(0, change)
		
		Case 5: Base1 + Size1 < Base0
			Simplified for If: Else
		.......A-------B.......
		.C---D.................
		(Do nothing)
		
	*/
	"SELECT OffsetId FROM Offset WHERE FileId = ? FOR UPDATE", // lock all of file's offsets: fileId
	// for each offset in this file for the range {
		"SELECT Base FROM Offset WHERE OffsetId = ?", // get base (old_size shouldn't change): offsetId
		// if change != 0 {
			"WITH change AS ? \
			UPDATE Offset SET Base = CASE \
				WHEN OffsetId != ? AND Base >= ? THEN Base + ? \
				ELSE Base \
			END, Size = CASE \
				WHEN Base <= ? AND Base + Size >= ? THEN Size + ?\
				ELSE Size \
			END \
			WHERE FileId = ?", // Case 0, Case 3: change, offsetId, base + new_size, base, base + old_size, fileId
		// }
		"WITH B AS ?, S AS ? \
		UPDATE Offset SET Conflict = TRUE WHERE FileId = ? AND \
		(B >= Base AND (B + S <= Base + Size OR B < Base + Size)) OR \
		(B < Base AND B + S > Base AND B + S <= Base + Size)" // rest: base, new_size, fileId
		"UPDATE Offset SET Conflict = FALSE WHERE OffsetId = ?" // clear conflict of editted range: offsetId
	// }
}

MYSQL mysql;

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
		fprintf(stderr, "mysql connection failed\n");
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

struct range* query_select_named_range(char* name){ // free return value
	#define NUM_BIND 6
	struct range* r = NULL;
	MYSQL_STMT* stmt;
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	char old_buf[PATH_MAX + 1];
	unsigned long fileId, offsetId;
	size_t base, size;
	unsigned long len;
	int succ, i = 0;
	bool error;
	
	old_buf[0] = 0;
	len = strlen(name);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, (bool*)0, true, &error); // File.FileId
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, name, len, &len, (bool*)0, true, &error); // Range.Name
	mysql_bind_init(bind[2], MYSQL_TYPE_STRING, buf, PATH_MAX, &len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &offsetId, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &base, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Base
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &size, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Size
	
	if (!pps(&stmt, QUERY_SELECT_NAMED_RANGE, bind, &bind[1])){
		if (!mysql_stmt_execute(stmt)){
			if (!mysql_stmt_store_result(stmt)){
				if (r = malloc(sizeof(struct range))){
					if (!range_init(r, name)){
						for (;;){
							succ = mysql_stmt_fetch(stmt);
							fail_check(succ == 1);
							if (succ == MYSQL_NO_DATA){
								break;
							}
							if (!strncmp(old_buf, buf, len){ // different file; add it // TODO: replace with range_add_new_file?
								memcpy(old_buf, buf, len);
								oldbuf[len + 1] = 0;
								i = range_add_file(r, old_buf, fileId, /* TODO: Where to store range type? */);
								fail_check(!i);
							}
							// TODO: Conflict
							if (!it_insert(&r->files[i].it, base, size, offsetId)){
								goto fail;
							}
						}
					}
				}
			}
		}
	}
	goto pass;
fail:
	if (r){
		range_deinit(r);
		freec(r);
	}
	if (mysql_stmt_error(stmt)){
		fprintf(stderr, "%s\n", mysql_stmt_error(stmt));
		break;
	}
pass:
	if (stmt)
		mysql_stmt_close(stmt);
	return r;
	#undef NUM_BIND
}

int query_insert_named_range(struct range* r, char* user){
	#define NUM_STMT 6
	#define NUM_BIND 7
	int ret = 0, i, j;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	char buf[PATH_MAX + 1];
	struct it_node* p_itn, itn;
	unsigned long name_len, user_len;
	int succ;
	bool error;
	unsigned long rangeId, fileId;
	
	name_len = strlen(r->name);
	user_len = strlen(r->user);
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_STRING, r->name, name_len, &name_len, (bool*)0, true, &error); // Range.Name
	mysql_bind_init(bind[1], MYSQL_TYPE_STRING, user, user_len, &user_len, (bool*)0, true, &error); // Range.User
	mysql_bind_init(bind[2], MYSQL_TYPE_STRING, buf, name_len, &name_len, (bool*)0, true, &error); // File.FilePath
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &rangeId, sizeof(size_t), NULL, (bool*)0, true, &error); // RangeId
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, (bool*)0, true, &error); // FileId
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Base
	mysql_bind_init(bind[6], MYSQL_TYPE_LONGLONG, &itn.size, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.Size
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(stmt, QUERY_INSERT_NAMED_RANGE[0], bind, NULL) ||
		pps(&stmt[1], QUERY_INSERT_NAMED_RANGE[1], &bind[1], NULL) ||
		pps(&stmt[2], QUERY_INSERT_NAMED_RANGE[2], &bind[2], &bind[4]) ||
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
		fail_check(!mysql_stmt_execute(stmt[1]));
		if (mysql_stmt_affected_rows(stmt[1]) == 0){
			fail_check(!mysql_stmt_execute(stmt[2]));
			succ = mysql_stmt_fetch(stmt[2]);
			fail_check(succ == 1);
			if (succ == MYSQL_NO_DATA){
				fprintf(stderr, "Failed to add file %s\n", buf); // failed to add and failed to receive
				goto fail;
			}
		}
		else {
			fileId = mysql_insert_id(&mysql);
		}
		fail_check(!mysql_stmt_execute(stmt[3]));
		it_foreach_interval(&r->files[i].it, j, p_itn){
			memcpy(&itn, p_itn, sizeof(struct it_node));
			if (!mysql_stmt_execute(stmt[4])){
				fprintf(stderr, "Failed to insert interval (%d, %d) in file %s\n", itn.base, itn.base + itn.size, buf); // TODO: verbose?
			}
		}
	}
	fail_check(!mysql_stmt_execute(stmt[5]));
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	for (i = 0; i < NUM_STMT; i++){
		if (mysql_stmt_error(stmt[i])){
			fprintf(stderr, "%s\n", mysql_stmt_error(stmt[i]));
			break;
		}
	}
pass:
	for (i = 0; i < NUM_STMT; i++)
		if (stmt[i])
			mysql_stmt_close(stmt[i]);
	return ret;
	#undef NUM_STMT
	#undef NUM_BIND
}

int query_resize_file(struct range_file* f){
	#define NUM_STMT 5
	#define NUM_BIND 6
	int ret = 0, i;
	MYSQL_STMT* stmt[NUM_STMT];
	MYSQL_BIND bind[NUM_BIND];
	struct it_node* p_itn, itn;
	unsigned long change, bns, bos, fileId;
	int succ;
	bool error;
	
	memset(bind, 0, NUM_BIND * sizeof(MYSQL_BIND));
	mysql_bind_init(bind[0], MYSQL_TYPE_LONGLONG, &change, sizeof(size_t), NULL, (bool*)0, true, &error); // change
	mysql_bind_init(bind[1], MYSQL_TYPE_LONGLONG, &itn.offsetId, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.OffsetId
	mysql_bind_init(bind[2], MYSQL_TYPE_LONGLONG, &bns, sizeof(size_t), NULL, (bool*)0, true, &error); // base + new_size
	mysql_bind_init(bind[3], MYSQL_TYPE_LONGLONG, &itn.base, sizeof(size_t), NULL, (bool*)0, true, &error); // base
	mysql_bind_init(bind[4], MYSQL_TYPE_LONGLONG, &bos, sizeof(size_t), NULL, (bool*)0, true, &error); // base + old_size, new_size
	mysql_bind_init(bind[5], MYSQL_TYPE_LONGLONG, &fileId, sizeof(size_t), NULL, (bool*)0, true, &error); // Offset.FileId
	
	memset(stmt, 0, NUM_STMT * sizeof(MYSQL_STMT*));
	fail_check(
		pps(stmt, QUERY_RESIZE_FILE[0], &bind[5], NULL) ||
		pps(&stmt[1], QUERY_RESIZE_FILE[1], &bind[1], &bind[3]) ||
		pps(&stmt[2], QUERY_RESIZE_FILE[2], &bind[0], NULL) ||
		pps(&stmt[3], QUERY_RESIZE_FILE[3], &bind[3], NULL) ||
		pps(&stmt[4], QUERY_RESIZE_FILE[4], &bind[1], NULL)
	);
	
	TXN_START;
	
	fail_check(!mysql_stmt_execute(stmt[0]));
	it_foreach_interval(&f->it, i, p_itn){ // TODO: write to file at the same time? Should definitely be done w/i transaction
		memcpy(&itn, p_itn, sizeof(struct it_node));
		fail_check(!mysql_stmt_execute(stmt[1]));
		succ = mysql_stmt_fetch(stmt[1]);
		fail_check(succ == 1);
		if (succ == MYSQL_NO_DATA){
			fprintf(stderr, "Failed to get base for offset %d\n", i);
			goto fail;
		}
		// TODO: fill change
		if (!change){
			bns = itn.base + itn.size;
			// TODO: fill bos (need old_size)
			fail_check(!mysql_stmt_execute(stmt[2]));
		}
		bos = itn.size;
		fail_check(!mysql_stmt_execute(stmt[3]));
		fail_check(!mysql_stmt_execute(stmt[4]));
	}
	
	TXN_COMMIT;
	goto pass;
fail:
	TXN_ROLLBACK;
	ret = -1;
	for (i = 0; i < NUM_STMT; i++){
		if (mysql_stmt_error(stmt[i])){
			fprintf(stderr, "%s\n", mysql_stmt_error(stmt[i]));
			break;
		}
	}
pass:
	for (i = 0; i < NUM_STMT; i++)
		if (stmt[i])
			mysql_stmt_close(stmt[i]);
	return ret;
	#undef NUM_BIND
	#undef NUM_STMT
}
