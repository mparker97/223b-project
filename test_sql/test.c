#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A_LIST_UNION(t, n0, n1, n2) \
union{ \
	struct{ \
		t* n0; \
		int n1; \
	}; \
	struct a_list n2; \
}

#define A_LIST_INIT_LEN 4
struct a_list{ // "amortized" list (contiguous array); initial size of A_LIST_INIT_LEN, doubles automatically when filled
	void* ls;
	int sz;
};

char* mystrdup(char* s){
  size_t l = strlen(s);
  char* ret = malloc(l + 1);
  if (ret){
    memcpy(ret, s, l + 1);
  }
  return ret;
}

static void* a_list_init(struct a_list* ls, size_t elm_sz){
	if (!ls->ls){
		if (!(ls->ls = calloc(A_LIST_INIT_LEN, elm_sz))){
			return NULL;
		}
		ls->sz = 0;
	}
	return ls->ls;
}

static void a_list_deinit(struct a_list* ls){
	if (ls->ls){
		free(ls->ls);
		ls->ls = NULL;
		ls->sz = 0;
	}
}

static void* a_list_add(struct a_list* ls, size_t elm_sz){
	if (!(ls->sz & (ls->sz - 1)) && ls->sz >= A_LIST_INIT_LEN){
		if (!(ls->ls = realloc(ls->ls, ls->sz * 2 * elm_sz))){
			return NULL;
		}
	}
	return &((char*)(ls->ls))[ls->sz++ * elm_sz];
}

struct range_file{
	char* file_path;
	unsigned long id;
	int num_it;
};

struct range{
	A_LIST_UNION(struct range_file, files, num_files, ls);
	char* name;
	unsigned long id;
};

struct range_file* range_add_file(struct range* r, char* file_path, unsigned long id){
	struct range_file* rf;
	char* str;
	if (strlen(file_path) > 4096)
		goto fail;
	if ((str = mystrdup(file_path)) == NULL){
		goto fail;
	}
	rf = a_list_add(&r->ls, sizeof(struct range_file));
	if (rf){
		rf->file_path = str;
		rf->id = id;
		rf->num_it = 0;
		return rf;
	}
fail:
	if (str)
		free(str);
	return NULL;
}

int range_init(struct range* r, char* name){
	if (!(r->name = mystrdup(name)))
		goto fail;
	if (!a_list_init(&r->ls, sizeof(struct range_file)))
		goto fail;
	return 0;
fail:
	fprintf(stderr, "Failed to initialize range\n");
	//range_deinit(r);
	return -1;
}

int main(void) {
  struct range r;
  if (range_init(&r, "whatever") < 0)
    printf("fail\n");
  else{
    range_add_file(&r, "path", 1);
    range_add_file(&r, "ewgrefd", 2);
    range_add_file(&r, "sbdbf", 3);
  }
  return 0;
}
