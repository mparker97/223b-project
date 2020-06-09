// cat UNPARSED_FILE | ./bo RANGE_LIST_FILE PARSED_FILE > PARSED_FILE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

const char* RANGE_W = "Range";
#define RANGE_W_LEN 5

const char* TOKENS = "\x1\x2\x3\x4\x5\x6\x7\x8\xb\xc\xe\xf\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";
#define TOKENS_LEN 28
static ssize_t interval_v[TOKENS_LEN];
char* lines[TOKENS_LEN];
char* line, *prog_name;
FILE* f_range;
static int add_file_bitv = 0;

void err(int x){
	int i;
	if (line)
		free(line);
	for (i = 0; i < TOKENS_LEN; i++){
		if (lines[i]){
			free(lines[i]);
		}
	}
	if (f_range)
		fclose(f_range);
	exit(x);
}

void* realloc_safe(void* p, size_t sz){
	p = realloc(p, sz);
	if (!p){
		fprintf(stderr, "%s: ERROR: Malloc failed\n", prog_name);
		err(1);
	}
	return p;
}

void read_lines(){
	char* line;
	size_t n, len;
	ssize_t s;
	int i = 0, j;
	for (line = NULL; (s = getline(&line, &n, f_range)) >= 0 && i < TOKENS_LEN; i++){
		if (line[0] != '\n'){
			len = strlen(line);
			if (!strncmp(line, RANGE_W, RANGE_W_LEN) && len >= RANGE_W_LEN + 3){
				j = atoi(line + RANGE_W_LEN) - 1;
				if (j >= 0 && j < TOKENS_LEN){
					if (lines[j] != NULL){
						fprintf(stderr, "%s:%d: ERROR: Duplicate range %s%d\n", prog_name, i, RANGE_W, j);
						err(1);
					}
					lines[j] = strndup(line, len - 1);
					continue;
				}
			}
			fprintf(stderr, "%s:%d: ERROR: Improperly formatted\n", prog_name, i);
			err(1);
		}
	}
	free(line);
	if (i == TOKENS_LEN && s >= 0){
		fprintf(stderr, "%s: WARNING: Ignoring after line %d\n", prog_name, i);
	}
	for (i = 0; i < TOKENS_LEN; i++){
		interval_v[i] = -1;
	}
}

void write_lines(){
	int i;
	long t;
	fseek(f_range, 0, SEEK_SET);
	for (i = 0; i < TOKENS_LEN; i++){
		if (lines[i]){
			fprintf(f_range, "%s\n", lines[i]);
			if (interval_v[i] >= 0){
				fprintf(stderr, "%s: WARNING: %s%02d has an unclosed interval starting at %ld\n", prog_name, RANGE_W, i, interval_v[i]);
			}
		}
	}
	t = ftell(f_range);
	fflush(f_range);
	if (t < 0){
		fprintf(stderr, "%s: ERROR: Ftell failed\n", prog_name);
		err(1);
	}
	else if (t > 0){
		ftruncate(fileno(f_range), t);
	}
}

void add_range(char* fp, size_t offset, int w){
	char buf[64];
	size_t len, buflen;
	if (lines[w] == NULL){ // Range has not been added yet
		lines[w] = realloc_safe(lines[w], RANGE_W_LEN + 2 + 1);
		sprintf(lines[w], "%s%02d", RANGE_W, w + 1);
	}
	len = strlen(lines[w]);
	if (!(add_file_bitv & (1U << w))){ // File has not been added yet for this range
		add_file_bitv |= (1U << w);
		lines[w] = realloc_safe(lines[w], len + 4 + strlen(fp) + 3 + 1);
		sprintf(lines[w] + len, " -f %s -r", fp);
	}
	if (interval_v[w] >= 0){ // Closing mark found; insert interval
		
		snprintf(buf, 63, " %ld,%lu", interval_v[w], offset);
		interval_v[w] = -1;
		buflen = strlen(buf);
		lines[w] = realloc_safe(lines[w], len + buflen + 1);
		strcpy(lines[w] + len, buf);
	}
	else{ // Opening mark found; save it
		interval_v[w] = offset;
	}
}

int main(int argc, char* argv[]){
	char* str;
	size_t c = 0, n;
	ssize_t s, i;
	int rcount;
	prog_name = argv[0];
	if (argc != 3){
		fprintf(stderr, "%s: ERROR: run as cat UNPARSED_FILE | %s RANGE_LIST_FILE PARSED_FILE > PARSED_FILE\n", prog_name, prog_name);
		return 1;
	}
	if ((rcount = open(argv[1], O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IXOTH)) < 0){
		goto open_fail;
	}
	if (!(f_range = fdopen(rcount, "r+"))){
		close(rcount);
		goto open_fail;
	}
	read_lines();
	for (line = NULL, rcount = 0; (s = getline(&line, &n, stdin)) >= 0;){
		for (i = 0; i < s; i++){
			str = strchr(TOKENS, line[i]);
			if (str){
				add_range(argv[2], c, str - TOKENS);
				rcount++;
			}
			else if (line[i] != 0xd){ // skip Windows CR
				fprintf(stdout, "%c", line[i]);
				c++;
			}
		}
	}
	if (rcount == 0){
		fprintf(stderr, "%s: WARNING: No regions found in input file\n", prog_name);
	}
	write_lines();
	err(0);
open_fail:
	fprintf(stderr, "%s: ERROR: Failed to open range list file %s\n", prog_name, argv[1]);
	err(1);
}