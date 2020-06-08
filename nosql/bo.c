#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* TOKENS = "\x1\x2\x3\x4\x5\x6\x7\x8\xb\xc\xe\xf\x10\x11\x12\x13\x14\x15";

int main(int argc, char* argv[]){
	char* line, *str;
	size_t c = 0, n;
	ssize_t s, i;
	int rcount = 0;
	for (line = NULL; (s = getline(&line, &n, stdin)) >= 0;){
		for (i = 0; i < s; i++){
			if (strchr(TOKENS, line[i])){
				fprintf(stderr, "Range %d:  %lu\n", line[i], c);
				rcount++;
			}
			else if (line[i] != 0xd){ // skip Windows CR
				fprintf(stdout, "%c", line[i]);
				c++;
			}
		}
	}
	free(line);
	if (rcount == 0){
		fprintf(stderr, "WARNING: No regions found in this file\n");
	}
	return 0;
}