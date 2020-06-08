// ./bo RANGE_LIST_FILE UNPARSED_FILE > PARSED_FILE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

const char* TOKENS = "\x1\x2\x3\x4\x5\x6\x7\x8\xb\xc\xe\xf\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";
#define TOKENS_LEN 28
static ssize_t interval_v[TOKENS_LEN];
char* lines[TOKENS_LEN];
char* line, *prog_name;
FILE* f_unparsed;
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
	if (f_unparsed)
		fclose(f_unparsed);
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
	size_t n;
	ssize_t s;
	int i = 0;
	for (; (s = getline(&lines[i], &n, f_range)) >= 0 && i < TOKENS_LEN; i++);
	if (i == TOKENS_LEN && s >= 0){
		fprintf(stderr, "%s: ERROR: Too many lines\n", prog_name);
		err(1);
	}
	for (i = 0; i < TOKENS_LEN; i++){
		fprintf(stderr, "line[%d] (%p): %s\n", i, lines[i], lines[i]);
		interval_v[i] = -1;
	}
}

void write_lines(){
	int i;
	fseek(f_range, 0, SEEK_SET);
	for (i = 0; i < TOKENS_LEN; i++){
		if (lines[i]){
			fwrite(lines[i], sizeof(char), strlen(lines[i]), f_range);
			if (interval_v[i] >= 0){
				fprintf(stderr, "%s: WARNING: Range %d has an unclosed interval starting at %ld\n", prog_name, i, interval_v[i]);
			}
		}
		fwrite("\n", sizeof(char), 1, f_range);
	}
	ftruncate(fileno(f_range), ftell(f_range));
}

void add_range(char* fp, size_t offset, int w){
	char buf[64];
	size_t len, buflen;
	if (lines[w] == NULL){ // Range has not been added yet
		lines[w] = realloc_safe(lines[w], 8);
		sprintf(lines[w], "Range%02d", w);
	}
	len = strlen(lines[w]);
	if (!(add_file_bitv & (1U << w))){ // File has not been added yet for this range
		add_file_bitv |= (1U << w);
		lines[w] = realloc_safe(lines[w], len + 4 + strlen(fp) + 3);
		sprintf(lines[w] + len, " -f %s -r", fp);
	}
	if (interval_v[w] >= 0){ // Closing mark found; insert interval
		snprintf(buf, 63, " %ld,%lu", interval_v[w], offset);
		interval_v[w] = -1;
		buflen = strlen(buf);
		lines[w] = realloc_safe(lines[w], len + buflen);
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
	int rcount = 0;
	prog_name = argv[0];
	if (argc != 3){
		fprintf(stderr, "%s: ERROR: Must be run with input file path and output range list file path\n", prog_name);
		return 1;
	}
	if (!(f_range = fopen(argv[1], "w+"))){
		fprintf(stderr, "%s: ERROR: Failed to open range list file %s\n", prog_name, argv[1]);
		err(1);
	}
	if (!(f_unparsed = fopen(argv[2], "r"))){
		fprintf(stderr, "%s: ERROR: Failed to open unparsed file %s\n", prog_name, argv[2]);
		err(1);
	}
	read_lines();
	for (line = NULL; (s = getline(&line, &n, f_unparsed)) >= 0;){
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
		fprintf(stderr, "%s: WARNING: No regions found in file %s\n", prog_name, argv[2]);
	}
	write_lines();
	err(0);
	return 0;
}