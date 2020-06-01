#ifndef ZKCLIENT
#define ZKCLIENT

#include <zookeeper/zookeeper.h>
#include <pthread.h>

// callback function
typedef void (* zk_lock_callback) (void* cb_data);
typedef struct zk_lock_context {
    char* lock_name;
    char* parent_path;
    size_t base;
    size_t size;
    size_t version;
    int owner;  // non-zero if lock was acquired
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