#include <string.h>

int func1(int a){
	return a + 3;
}

static char* func2(char* s, int b){
	size_t l = strlen(s);
	if (l > b){
		s[b] = 0;
	}
	return s;
}

int main(int argc, char* argv[]){
	char* s;
	int i, a;
	if (argc < 3){
		exit(1);
	}
	
	/*
		This is my beautiful procedure!
	*/
	for (i = 0; i < 10; i++){ // run 10 times
		/* func 1 section*/
		a = getSomeInt(argv[1], i);
		if (a < 12){
			a = func1(a);
			printf("%d\n", a);
		}
		/* func 2 section */
		s = getSomeString(argv[2], i);
		if (s){
			s = func2(s);
			printf("%s\n", s);
		}
	}
	return 0;
}