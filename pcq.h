#ifndef PCQ_H
#define PCQ_H
#include <pthread.h>
#define pcq_enqueue(x, y) pcq_enqueue_real(x, (char*)(y))

struct pcq{
	pthread_cond_t empty;
	pthread_cond_t full;
	pthread_mutex_t lock;
	char** q; // pointer to queue
	size_t sz; // max size
	size_t cap; // current capacity
};

static int pcq_init(struct pcq* q, size_t sz){
	if (!(q->q = malloc(sz * sizeof(char*)))){
		return -1;
	}
	q->sz = sz;
	q->cap = 0;
	pthread_mutex_init(&q->lock, NULL);
	pthread_cond_init(&q->empty, NULL);
	pthread_cond_init(&q->full, NULL);
	return 0;
}

static void pcq_deinit(struct pcq* q){
	if (q->q){
		freec(q->q);
		q->sz = q->cap = 0;
		pthread_mutex_destroy(&q->lock);
		pthread_cond_destroy(&q->empty);
		pthread_cond_destroy(&q->full);
	}
}

static void pcq_enqueue_real(struct pcq* q, char* elm){
	pthread_mutex_lock(&q->lock);
	while (q->cap == q->sz)
		pthread_cond_wait(&q->full, &q->lock);
	q->q[q->cap++] = elm;
	pthread_cond_signal(&q->empty);
	pthread_mutex_unlock(&q->lock);
}

static char* pcq_dequeue(struct pcq* q){
	char* ret;
	pthread_mutex_lock(&q->lock);
	while (q->cap == 0)
		pthread_cond_wait(&q->empty, &q->lock);
	ret = q->q[--(q->cap)];
	pthread_cond_signal(&q->full);
	pthread_mutex_unlock(&q->lock);
	return ret;
}

#endif