#include <errno.h>
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
        // TODO
        // double check children 
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
    sprintf(version_path, "%s/interval", path);
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
    sprintf(buf, "%s/interval/%d-", context->parent_path, context->offset_id);
    // 11 for the <SEQUENCE>
    char full_path[len + 11];

    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    if (ret != ZOK) {
        LOG_WARN(LOGCALLBACK(zh), "could not create zoo node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    // 2. GET CHILDREN, SHIFT INTERVALS AS NEEDED AND DISCARD THOSE NOT IN INTERVAL
    it_array_t relevant_intervals;
    int ret = _get_sorted_shifted_relevant_intervals(context, &relevant_intervals);
    // get children call failed in the helper function
    if (ret != ZOK) {
        return ret;
    }

    // 3. DETERMINE LOCK ELIGIBILITY
    // there are no conflicting locks - client has lock
    if (relevant_intervals.len == 0) {
        context->owner = 1;
        return ZOK;
    }

    context->owner = 0;

    // watch lock znode with smallest sequence number
    struct Stat stat;
    for (int i = 0; i < relevant_intervals.len; i++) {
        // 10 for "/interval/" + 1 for '\0' + 23 for "<offset_id>-<sequence>"
        int len = strlen(context->parent_path) + 34;
        char buf[len];
        sprintf(buf, "%s/interval/%d-%010d", 
                context->parent_path,
                relevant_intervals.array[i]->id,
                relevant_intervals.array[i]->sequence);
        ret = zoo_exists(zh, buf, 1, &stat);
        if (ret == ZOK) {
            return ret;
        }
        // error
        else if (ret != ZNONODE) {
            return ret;
        }
        // else - ret == ZNONODE -> continue
    }

    return -1;
}

static int _get_sorted_shifted_relevant_intervals(zk_lock_context_t* context, it_array_t* ret_array) {
    // get all existing interval locks <offset_id>-<sequence>
    // ideally this would be a map, but we'll just do a linear search for the time being
    struct String_vector all_intervals;
    all_intervals.data = NULL;
    all_intervals.count = 0;
    char interval_path[strlen(context->parent_path) + 10];
    sprintf(interval_path, "%s/interval", context->parent_path);
    int ret = zoo_get_children(zh, interval_path, 0, &all_intervals);
    if (ret != ZOK) {
        return ret;
    }

    // TODO

    // get intervals from mysql for this file conflicting with new_interval
    it_node_t new_interval = {
        .base = context->base,
        .bound = context->base + context->size - 1,
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
    }

    // sort array by sequence number
    // TODO

    // set return value
    ret_array->array = interval_array;
    ret_array->len = interval_count;
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
    char* lock_name = strrchr(lock_name, '/');
    if (lock_name == NULL) 
        return 0;

    char* endptr;
    errno = 0;
    size_t ret = strtoul(lock_name, &endptr, 10);
    if (errno != 0 || *endptr != 0) 
        return 0;

    return ret;
}

// TODO test
int main() {
    
    return 0;
}