#include <stdio.h>

int func(){
	return 3;
}

int getSomeString(char* s, int k){
	char* p = getAnotherString(s);
	if (p){
		p[0] = k;
	}
	return p[func()]; // I don't care
}

// comment