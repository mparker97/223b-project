#ifndef ZKCLIENT
#define ZKCLIENT

#include <zookeeper/zookeeper.h>
#include <pthread.h>

#define MASTER_LOCK_TYPE 0
#define INTERVAL_LOCK_TYPE 1

// callback function
typedef void (* zk_lock_callback) (void* cb_data);
typedef struct zk_lock_context {
    char* lock_name;
    char* parent_path;
    unsigned long offset_id;
    size_t base;
    size_t bound;
    // non-zero if lock was acquired
    int owner;  
    // callback functions after watcher determines lock has been acquired
    zk_lock_callback cb_fn;
    void* cb_data;

} zk_lock_context_t;

// FUNCTION DECLARATIONS
void watcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
int zk_release_interval_lock(zk_lock_context_t *context);
int zk_acquire_interval_lock(zk_lock_context_t *context);
static int _zk_interval_lock_operation(zk_lock_context_t *context, struct timespec *ts);
static int _zk_determine_interval_lock_eligibility(zk_lock_context_t *context, struct timespec *ts);
static int _get_sorted_shifted_relevant_intervals(zk_lock_context_t* context, it_array_t* ret_array);
static it_node_t** _sort_interval_locks_by_offset_id(struct String_vector * interval_children);
static void _free_intervals_array(it_node_t** intervals_array, int len);
static char* getLockName(char* str);
static size_t getSequenceNumber(char* lock_name);
int cmpSequenceFunc(const void * a, const void * b);
int cmpOffsetIdFunc(const void * a, const void * b);

#endif