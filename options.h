#ifndef OPTIONS_H
#define OPTIONS_H
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
//#define LN_LEN_MIN (4 + ORACLE_LEN_MIN * 2)

static const char* OPTIONS_FILE = "~/.whateverrc";
static const char* RC_TOKENS = " /t";
static char* NULL_STR = "";

void get_fname_ext(char** fname, char** ext, char* file_path){
	char* s = strrchr(file_path, '/');
	if (s){
		*fname = s + 1;
		s = strchr(s, '.');
		if (s){
			*ext = s;
		}
		else{
			*ext = NULL_STR;
		}
	}
	else{
		*fname = file_path;
	}
}

char* get_an_oracle(char** str, int options_line){
	char* s = NULL, *t;
	size_t len;
	for (t = *str; strpbrk(t, RC_TOKENS) == t; t++);
	if (t){
		s = pull_string(t);
		if (s){
			t++;
		}
		else if (t[0]){
			s = strpbrk(t, RC_TOKENS);
			if (!s){
				goto out_fail;
			}
			s[0] = 0;
			s++;
		}
		len = s - t - 1;
		if (len < ORACLE_LEN_MIN){
			fprintf(stderr, "%s:%d: Oracle is below the minimum oracle length of %d\n", OPTIONS_FILE + 2, options_line, ORACLE_LEN_MIN);
			s = NULL;
		}
		else if (len > ORACLE_LEN_MAX){
			fprintf(stderr, "%s:%d: Oracle is above the maximum oracle length of %d\n", OPTIONS_FILE + 2, options_line, ORACLE_LEN_MAX);
			s = NULL;
		}
		goto out_pass;
	}
out_fail:
	fprintf(stderr, "%s:%d: Improperly formed Oracle\n", OPTIONS_FILE + 2, options_line);
out_pass:
	*str = t;
	return s;
}

void get_oracles(char* file_path){
	FILE* f;
	char* target = NULL, *line = NULL, *str, *fname, *ext;
	size_t n, new_sz;
	int target_level = 0, new_target, i, options_line;
	f = fopen(OPTIONS_FILE, "r");
	if (!f){
		fprintf(stderr, "%s not found\n", OPTIONS_FILE);
		goto default_oracles;
	}
	get_fname_ext(&fname, &ext, file_path);
	for (i = 0; getline(&line, &n, f) >= 0; i++){
		if (n < 4)
			continue;
		str = strtok(line, RC_TOKENS);
		if (str){
			new_sz = n - strlen(str) - 1; // take off \n
			new_target = target_level;
			switch (target_level){
				case 0:
					if (str[0] == '*' && strcmp(str + 1, ext)){
						target = realloc(target, new_sz);
						target_level++;
					}
				case 1:
					if (strcmp(str, fname)){
						target = realloc(target, new_sz);
						target_level++;
					}
				case 2:
					if (!strcmp(str, file_path)){
						target = realloc(target, new_sz);
						target_level++;
					}
			}
			if (new_target > target_level){
				if (target){ // realloc check
					memcpy(target, line, new_sz);
					options_line = i;
				}
				if (target_level > 2){
					break;
				}
			}
		}
	}
	free(line);
	if (!target){
		goto default_oracles;
	}
	str = get_an_oracle(&target, options_line);
	if (str){
		line = get_an_oracle(&str, options_line);
		if (line){
			goto end;
		}
	}
	
	goto end;
default_oracles:
	fprintf(stderr, "Using default oracles\n");
	target = DEFAULT_START_ORACLE;
	str = DEFAULT_END_ORACLE;
end:
	strcpy(start_oracle, target);
	strcpy(end_oracle, str);
	if (f)
		fclose(f);
	if (target)
		free(target);
}

#endif