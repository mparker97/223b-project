#ifndef ZKCLIENT
#define ZKCLIENT

#include <zookeeper/zookeeper.h>
#include <pthread.h>

#define LOCK_TYPE_INTERVAL 0
#define LOCK_TYPE_MASTER_WRITE 1
#define LOCK_TYPE_MASTER_READ 1

// callback function
typedef void (* zk_lock_callback) (void* cb_data);

// FUNCTION DECLARATIONS
void watcher(zhandle_t *zzh, int type, int state, const char *path, void* context);
int zk_release_lock(range_file_t *context, int lock_type);
int zk_acquire_lock(range_file_t *context, int lock_type);

// master lock functions
static int _zk_master_read_lock_operation(range_file_t *context, struct timespec *ts);
static int _zk_master_write_operation(range_file_t *context, struct timespec *ts);

// interval lock functions
static int _zk_interval_lock_operation(range_file_t *context, struct timespec *ts);
static int _zk_determine_interval_lock_eligibility(range_file_t *context, struct timespec *ts);
static int _get_sorted_shifted_relevant_intervals(range_file_t* context, it_array_t* ret_array);
static it_node_t** _sort_interval_locks_by_offset_id(struct String_vector * interval_children);
static void _free_intervals_array(it_node_t** intervals_array, int len);

// util helpers
static char* getLockName(char* str);
static size_t getSequenceNumber(char* lock_name);
int cmpSequenceFunc(const void * a, const void * b);
int cmpOffsetIdFunc(const void * a, const void * b);
int cmpAlphabetical(const void * a, const void * b);

#endif