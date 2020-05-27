#ifndef _REGEX_H
#define _REGEX_H
#include <regex.h>
#include <sys/types.h>
#define REGEX_WINDOW_SIZE 2048 // maximum match length
#define REGEX_CFLAGS REG_EXTENDED
#define REGEX_EFLAGS (REG_NOTBOL | REG_NOTEOL)

void regex_error(regex_t* r, int e){
	char buf[256];
	regerror(e, r, buf, 256);
	fprintf(stderr, "Regex error: %s\n", buf);
}

int regex_test(char* pat){ // verify that pattern pat is valid regex and has at most one capture
	regex_t preg;
	int ret, e, sb_stk;
	switch (pat[0]){
		case '(':
			ret = 1;
			break;
		case '[':
			sb_stk = 1;
			break;
		default:
			ret = sb_stk = 0;
	}
	for (e = 1; e < strlen(pat); e++){
		switch (pat[e]){
			case '(':
				if (sb_stk == 0 && pat[e - 1] != '\\'){
					if (ret == 1){
						fprintf(stderr, "Regex error: at most one capture is allowed\n");
						return -1;
					}
				}
				break;
			case '[':
				if (sb_stk == 0 && pat[e - 1] != '\\'){
					sb_stk = 1;
				}
				break;
			case ']':
				if (pat[e - 1] != '\\'){
					sb_stk = 0;
				}
		}
	}
	ret = 0;
	e = regex_comp(&preg, pat, REGEX_CFLAGS);
	if (e){
		regex_error(&preg, e);
		ret = -1;
	}
	regfree(&preg);
}

static int do_regexec(size_t* base, size_t* size, regex_t* preg, char* s){
	int ret;
	regmatch_t pmatch[2];
	ret = regexec(preg, s, 2, pmatch, REGEX_EFLAGS);
	if (!ret){ // match found
		if (pmatch[1].re_so >= 0){
			*base = pmatch[1].rm_so;
			*size = pmatch[1].rm_eo;
		}
		else{
			*base = pmatch[0].rm_so;
			*size = pmatch[0].rm_eo;
		}
	}
	return ret;
}

int file_search_regex(size_t* base, size_t* size, int fd, char* pat){ // perform regex pattern pat on file identified by descriptor fd; fill base and size 
	char buf[REGEX_WINDOW_SIZE * 2 + 1];
	regex_t preg;
	size_t off = 0;
	ssize_t num_bytes;
	int ret = 0, e;
	buf[REGEX_WINDOW_SIZE * 2] = 0;
	e = regex_comp(&preg, pat, REGEX_CFLAGS);
	if (e){
		regex_error(&preg, e);
		ret = -1;
	}
	else{
		for (;;){
			memcpy(buf, buf + REGEX_WINDOW_SIZE, REGEX_WINDOW_SIZE);
			num_bytes = pread(fd, buf + REGEX_WINDOW_SIZE, REGEX_WINDOW_SIZE, off);
			if (num_bytes <= 0){
				ret = -1;
				break;
			}
			if (!do_regexec(base, size, preg, buf)){
				*base += off;
				*size += off;
				break;
			}
			off += num_bytes;
		}
	}
	regfree(&preg);
	return ret;
}

int regex_match(size_t* base, size_t* size, char* s, size_t s_len, char* pat){ // perform regex pattern pat on string s of length s_len; fill base and size
	regex_t preg;
	int ret = 0, e;
	e = regex_comp(&preg, pat, REGEX_CFLAGS);
	if (e){
		regex_error(&preg, e);
		ret = -1;
	}
	else if (do_regexec(base, size, &preg, s)){
		ret = -1;
	}
	regfree(&preg);
	return ret;
}

#endif