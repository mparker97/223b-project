#ifndef OPTIONS_H
#define OPTIONS_H
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
//#define LN_LEN_MIN (4 + ORACLE_LEN_MIN * 2)

static const char* OPTIONS_FILE = "~/.whateverrc";
static const char* RC_TOKENS = " /t";
static const char* NULL_STR = "";

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

char* get_an_oracle(char** str){
	char* s = NULL, *t;
	for (t = *str; strpbrk(t, RC_TOKENS) == t; t++);
	if (t){
		if (s){
			t++;
		}
		else if (t[0]){
			s = strpbrk(t, RC_TOKENS); // TODO
			if (s){
				s = t + strlen(t);
			}
			else{
				s = NULL;
			}
		}
	}
	*str = t;
	return s;
}

void get_oracles(char* file_path){
	FILE* f;
	char* target = NULL, *line = NULL, *str, *fname, *ext;
	size_t n, new_sz;
	int target_level = 0;
	int new_target;
	f = fopen(OPTIONS_FILE, "r");
	if (!f){
		fprintf(stderr, "%s not found; using default oracles\n", OPTIONS_FILE);
		goto default_oracles;
	}
	get_fname_ext(&fname, &ext, file_path);
	while (getline(&line, &n, f) >= 0){
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
				if (target){
					memcpy(target, line, new_sz);
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
	str = get_an_oracle(target);
	if (str){
		line = get_an_oracle(str);
		if (line){
			goto end;
		}
	}
	
	
	goto end;
default_oracles:
	target = DEFAULT_START_ORACLE;
	str = DEFAULT_END_ORACLE;
end:
	strcpy(start_oracle, target);
	strcpy(end_oracle, src);
	if (f)
		fclose(f);
	if (target)
		free(target);
}

#endif
