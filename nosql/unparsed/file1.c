#include <stdio.h>
#include <stdlib.h>

int getSomeInt(char* s, int k){
	int ret = atoi(s);
	if (ret > 0)
		return ret + k;
	else return -1;
}