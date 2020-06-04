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
    // 0 if "master" lock, 1 if "interval" lock
    int lock_type;
    zk_lock_callback cb_fn;
    void* cb_data;

} zk_lock_context_t;

// FUNCTION DECLARATIONS
void watcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
int zk_release_interval_lock(zk_lock_context_t *context);
int zk_acquire_interval_lock(zk_lock_context_t *context);
static int _zk_interval_lock_operation(zk_lock_context_t *context, struct timespec *ts);
static char* getLockName(char* str);

#endif