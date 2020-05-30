// Scrapped
#if 0
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

static int do_regex_comp(regex_t* preg, char* pat){
	int e = regex_comp(preg, pat, REGEX_CFLAGS);
	if (e){
		regex_error(preg, e);
		return -1;
	}
	return 0;
}

int regex_test(char* pat){ // verify that pattern pat is valid regex and has at most one capture
	regex_t preg;
	int ret = 0, i, sb_stk;
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
	for (i = 1; i < strlen(pat); i++){
		switch (pat[i]){
			case '(':
				if (sb_stk == 0 && pat[i - 1] != '\\'){
					if (ret == 1){
						fprintf(stderr, "Regex error: at most one capture is allowed\n");
						ret = -1;
						goto out;
					}
				}
				break;
			case '[':
				if (sb_stk == 0 && pat[i - 1] != '\\'){
					sb_stk = 1;
				}
				break;
			case ']':
				if (pat[i - 1] != '\\'){
					sb_stk = 0;
				}
		}
	}
	ret = do_regex_comp(&preg, pat);
out:
	regfree(&preg);
	return ret;
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
	ssize_t n_bytes;
	int ret = -1;
	if (!do_regex_comp(&preg, pat)){
		n_bytes = pread(fd, buf, REGEX_WINDOW_SIZE * 2, off);
		while (n_bytes > 0){
			buf[n_bytes] = 0;
			if (!do_regexec(base, size, &preg, buf)){
				*base += off;
				*size += off;
				ret = 0;
				break;
			}
			off += n_bytes;
			memcpy(buf, buf + REGEX_WINDOW_SIZE, REGEX_WINDOW_SIZE);
			n_bytes = pread(fd, buf + REGEX_WINDOW_SIZE, REGEX_WINDOW_SIZE, off);
		}
	}
	regfree(&preg);
	return ret;
}

int regex_match(size_t* base, size_t* size, char* s, size_t s_len, char* pat){ // perform regex pattern pat on string s of length s_len; fill base and size
	regex_t preg;
	int ret = 0;
	if (!do_regex_comp(&preg, pat)){
		if (do_regexec(base, size, &preg, s)){
			ret = -1;
		}
	}
	regfree(&preg);
	return ret;
}

// Verify that regex patterns still define the interval...
//   not so valuable because changes of other regions earlier in the file could turn into regex matches.
/*
int regex_before(int fd, size_t off, char* pat){
	char buf[REGEX_WINDOW_SIZE + 1];
	
}

ssize_t regex_after(int fd, size_t off, char* pat){
	char buf[REGEX_WINDOW_SIZE + 1];
	size_t base, size;
	n_bytes = pread(fd, buf, REGEX_WINDOW_SIZE, off);
	if (n_bytes > 0){
		buf[n_bytes] = 0;
	}
	if (!regex_match(&base, &size, buf, REGEX_WINDOW_SIZE, pat)){
		if (base != 0){ // check if regex match is immediately after oracle
			goto out;
		}
	}
	fprintf(stderr, "warning: regex does not match immediately after oracle\n");
	regfree(&preg);
}
*/

#endif
#endif