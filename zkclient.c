#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "sql.h"
#include "interval_tree.h"
#include "range.h"
#include "zkclient.h"

static zhandle_t *zh;
static clientid_t myid;

/**
 * Watcher function that gets called on event trigger
 */
void watcher(zhandle_t *zzh, int type, int state, const char *path,
             void* context) {
    fprintf(stderr, "Watcher %s state = %s", type2String(type), state2String(state));
    if (path && strlen(path) > 0) {
      fprintf(stderr, " for path %s", path);
    }
    fprintf(stderr, "\n");

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            const clientid_t *id = zoo_client_id(zzh);
            if (myid.client_id == 0 || myid.client_id != id->client_id) {
                myid = *id;
                fprintf(stderr, "Got a new session id: 0x%llx\n",
                        (long long) myid.client_id);
            }
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            fprintf(stderr, "Authentication failure. Shutting down...\n");
            zookeeper_close(zzh);
            zh=0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            fprintf(stderr, "Session expired. Shutting down...\n");
            zookeeper_close(zzh);
            zh=0;
        }
    }
    else if (type == ZOO_DELETED_EVENT) {
        zk_lock_context_t* zkcontext = (zk_lock_context_t*) context;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (.5)*1000000;

        if (zkcontext->lock_type == MASTER_LOCK_TYPE) {
            // TODO
            // get all children on parent znode
        }
        // zkcontext->lock_type == INTERVAL_LOCK_TYPE
        else {
            // helper function will set watch if not owner
            _zk_determine_interval_lock_eligibility(zkcontext, &ts);
            if (zkcontext->owner) {
                // successfully acquired lock
                zkcontext->cb_fn(zkcontext->cb_data);
            }
        } 
    }
}

int zk_release_interval_lock(zk_lock_context_t *context) {
    if (context->lock_name != NULL && context->parent_path != NULL) {
        int len = strlen(context->parent_path) + strlen(context->lock_name) + 2;
        char buf[len];
        sprintf(buf, "%s/%s", context->parent_path, context->lock_name);

        int ret = 0;
        int count = 0;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (.5)*1000000;
        ret = ZCONNECTIONLOSS;
        while (ret == ZCONNECTIONLOSS && (count < 3)) {
            ret = zoo_delete(zh, buf, -1);
            if (ret == ZCONNECTIONLOSS) {
                LOG_DEBUG(LOGCALLBACK(zh), ("connectionloss while deleting the node"));
                nanosleep(&ts, 0);
                count++;
            }
        }
        if (ret == ZOK || ret == ZNONODE) {
            return 0;
        }
        LOG_WARN(LOGCALLBACK(zh), ("not able to connect to server - giving up"));
        return ZCONNECTIONLOSS;
    }

    LOG_WARN(LOGCALLBACK(zh), ("either parent path or lock name is NULL"));
	return ZSYSTEMERROR;
}

int zk_acquire_interval_lock(zk_lock_context_t *context) {
    char *path = context->parent_path;
    struct Stat stat;
    int exists = zoo_exists(zh, path, 0, &stat);
    int count = 0;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (.5)*1000000;

    // retry to see if the parent path exists and 
    // and create if the parent path does not exist
    while ((exists == ZCONNECTIONLOSS || exists == ZNONODE) && (count < 3)) {
        count++;
        // retry the operation
        if (exists == ZCONNECTIONLOSS) 
            exists = zoo_exists(zh, path, 0, &stat);
        else if (exists == ZNONODE) 
            exists = zoo_create(zh, path, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
        nanosleep(&ts, 0);        
    }
    if (exists == ZCONNECTIONLOSS) {
        return exists;
    }

    // create subnode .../foo/interval if it doesn’t exist before
    char interval_path[strlen(path) + 10];
    sprintf(interval_path, "%s/interval", path);
    exists = zoo_exists(zh, interval_path, 0, &stat);
    count = 0;
    while ((exists == ZCONNECTIONLOSS || exists == ZNONODE) && (count < 3)) {
        count++;
        if (exists == ZCONNECTIONLOSS) 
            exists = zoo_exists(zh, interval_path, 0, &stat);
        else if (exists == ZNONODE) 
            exists = zoo_create(zh, interval_path, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
        nanosleep(&ts, 0);
    }
    if (exists == ZCONNECTIONLOSS) {
        return exists;
    }

    // attempt actual lock creation
    int check_retry = ZCONNECTIONLOSS;
    count = 0;
    while (check_retry != ZOK && count < 3) {
        check_retry = _zk_interval_lock_operation(context, &ts);
        if (check_retry != ZOK) {
            nanosleep(&ts, 0);
            count++;
        }
    }

    return check_retry;
}

static int _zk_interval_lock_operation(zk_lock_context_t *context, struct timespec *ts) {
    // 1. CREATE LOCK NODE WITH EPHEMERAL AND SEQUENCE FLAGS ON 
    // construct full lock file path: .../foo/interval/<VERSION>-<START>-<LENGTH>-<SEQUENCE>
    // 47 = 10 for "/interval/" + 1 for '\0' + 1 for '-' + 11 for offset_id number 
    int len = strlen(context->parent_path) + 23;
    char buf[len];
    sprintf(buf, "%s/interval/%lu-", context->parent_path, context->offset_id);
    // 11 for the <SEQUENCE>
    char full_path[len + 11];

    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    if (ret != ZOK) {
        LOG_WARN(LOGCALLBACK(zh), "could not create zoo node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    return _zk_determine_interval_lock_eligibility(context, ts);
}

static int _zk_determine_interval_lock_eligibility(zk_lock_context_t *context, struct timespec *ts) {
    // 2. GET CHILDREN, SHIFT INTERVALS AS NEEDED AND DISCARD THOSE NOT IN INTERVAL
    it_array_t relevant_intervals;
    int ret = _get_sorted_shifted_relevant_intervals(context, &relevant_intervals);
    // get children call failed in the helper function
    if (ret != ZOK) {
        return ret;
    }

    // 3. DETERMINE LOCK ELIGIBILITY
    // should not be empty since we created the lock before getChildren
    if (relevant_intervals.len == 0) {
        fprintf(stderr, "List of relevant interval nodes is empty");
        return -1;
    }

    // there are no conflicting locks - client has lock
    int cur_sequence = getSequenceNumber(context->lock_name);
    if (relevant_intervals.array[0]->sequence == cur_sequence) {
        context->owner = 1;
        return ZOK;
    }
    
    context->owner = 0;
    it_node_t new_interval = {
        .sequence = cur_sequence,
    };
    it_node_t* ni_ptr = &new_interval;
    it_node_t** found_interval = (it_node_t**) bsearch(
        &ni_ptr, 
        relevant_intervals.array, 
        relevant_intervals.len,
        sizeof(ni_ptr),
        cmpSequenceFunc
    );

    // watch lock znode with next smallest sequence number
    // since the cur_sequence is not the smallest, found_interval is definitely not at index 0
    found_interval--;

    // 10 for "/interval/" + 1 for '\0' + 23 for "<offset_id>-<sequence>"
    int len = strlen(context->parent_path) + 34;
    char buf[len];
    sprintf(buf, "%s/interval/%lu-%010d", context->parent_path,
            (*found_interval)->id, (*found_interval)->sequence);
    
    struct Stat stat;
    ret = zoo_wexists(zh, buf, watcher, (void*) context, &stat);
    int retry_count = 0;
    while (ret != ZOK && retry_count < 3) {
        ret = zoo_wexists(zh, buf, watcher, (void*) context, &stat);
        if (ret != ZOK) {
            nanosleep(ts, 0);
            retry_count++;
        }
    }

    return ret;
}

static int _get_sorted_shifted_relevant_intervals(zk_lock_context_t* context, it_array_t* ret_array) {
    ret_array->array = NULL;
    ret_array->len = 0;

    // get all existing interval locks <offset_id>-<sequence>
    struct String_vector interval_locks;
    interval_locks.data = NULL;
    interval_locks.count = 0;
    char interval_path[strlen(context->parent_path) + 10];
    sprintf(interval_path, "%s/interval", context->parent_path);
    int ret = zoo_get_children(zh, interval_path, 0, &interval_locks);
    if (ret != ZOK) {
        return ret;
    }

    // return straight away if empty
    if (interval_locks.count == 0) {
        return 0;
    }

    // sorted list of interval locks (<offset_id>-<sequence>)
    it_node_t** locks_sorted_by_offset_id = _sort_interval_locks_by_offset_id(&interval_locks);
    if (locks_sorted_by_offset_id == NULL) {
        return -1;
    }

    // get intervals from mysql for this file conflicting with new_interval
    it_node_t new_interval = {
        .base = context->base,
        .bound = context->bound,
    };
    struct range_file rf;
    ret = query_select_file_intervals(&rf, context->parent_path, &new_interval);
    if (ret == -1) {
        return ret;
    }    

    // get array of relevant intervals
    it_node_t* cur_interval;
    it_node_t** interval_array = NULL;
    int interval_count = 0;
    it_foreach(&rf.it, cur_interval){
        interval_count++;
        it_node_t** temp_array = realloc(
            interval_array, 
            interval_count * sizeof(it_node_t*)
        );
        if (temp_array == NULL) {
            return -1;
        }
        interval_array = temp_array;
        interval_array[interval_count-1] = cur_interval;

        // find corresponding sequence from zk for cur_interval
        // ideally this would be a map, but we'll just do a binary search for the time being
        it_node_t** found_interval = (it_node_t**) bsearch(
            &cur_interval, 
            locks_sorted_by_offset_id, 
            interval_locks.count,
            sizeof(cur_interval),
            cmpOffsetIdFunc
        );
        cur_interval->sequence = (*found_interval)->sequence;
    }

    // free the locks information with just the offset_id and sequence
    _free_intervals_array(locks_sorted_by_offset_id, interval_locks.count);

    // sort array by sequence number
    qsort(interval_array, interval_count, sizeof(it_node_t*), cmpSequenceFunc);

    // set return value
    ret_array->array = interval_array;
    ret_array->len = interval_count;
}

static it_node_t** _sort_interval_locks_by_offset_id(struct String_vector * interval_children) {
    it_node_t** intervals_array = malloc(interval_children->count * sizeof(it_node_t*));
    if (intervals_array == NULL) {
        return NULL;
    }

    for (int i=0; i < interval_children->count; i++) {
        it_node_t* cur_interval = malloc(sizeof(it_node_t));
        if (cur_interval == NULL) {
            _free_intervals_array(intervals_array, i);
            return NULL;
        }
        sscanf(interval_children->data[i], "%lu-%d", &(cur_interval->id), &(cur_interval->sequence));
        intervals_array[i] = cur_interval;
    }

    // sort array by offset_id
    qsort(intervals_array, interval_children->count, sizeof(it_node_t*), cmpOffsetIdFunc);

    return intervals_array;
}

static void _free_intervals_array(it_node_t** intervals_array, int len) {
    for (int i=0; i < len; i++) {
        free(intervals_array[i]);        
    }
    free(intervals_array);
}

// HELPER UTIL FUNCTIONS
/**
 * get the last name of the path
 */
static char* getLockName(char* str) {
    char* name = strrchr(str, '/');
    if (name == NULL) 
        return NULL;
    return strdup(name + 1);
}
/**
 * get the sequence number from the lock_name returned from zookeeper
 */
static size_t getSequenceNumber(char* lock_name) {
    lock_name = strrchr(lock_name, '/');
    if (lock_name == NULL) 
        return 0;

    char* endptr;
    errno = 0;
    size_t ret = strtoul(lock_name, &endptr, 10);
    if (errno != 0 || *endptr != 0) 
        return 0;

    return ret;
}

// compare function for it_node_t* elements by sequence
int cmpSequenceFunc(const void * a, const void * b) {
    return ((*(it_node_t**)a)->sequence - (*(it_node_t**)b)->sequence);
}

// compare function for it_node_t* elements by offset
int cmpOffsetIdFunc(const void * a, const void * b) {
    return ((*(it_node_t**)a)->id - (*(it_node_t**)b)->id);
}

// TODO test
// int main() {
    
//     return 0;
// }