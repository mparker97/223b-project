#include <errno.h>
#include "interval_tree.h"
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

    // get current version of file, or create one if it doesn't exist yet
    char version_path[strlen(path) + 9];
    sprintf(version_path, "%s/version", path);
    exists = zoo_exists(zh, version_path, 0, &stat);
    count = 0;
    while ((exists == ZCONNECTIONLOSS || exists == ZNONODE) && (count < 3)) {
        count++;
        if (exists == ZCONNECTIONLOSS) 
            exists = zoo_exists(zh, version_path, 0, &stat);
        else if (exists == ZNONODE) 
            exists = zoo_create(zh, version_path, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
        nanosleep(&ts, 0);
    }
    if (exists == ZCONNECTIONLOSS) {
        return exists;
    }
    context->version = stat.version;

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
    // 47 = 2 for '/' + 1 for '\0' + 8 for "interval" + 3 for '-' + 11 for each number v-s-l * 3
    int len = strlen(context->parent_path) + 47;
    char buf[len];
    sprintf(buf, "%s/interval/%d-%d-%d-", context->parent_path, context->version, 
            context->base, context->size);
    // 11 for the <SEQUENCE>
    char full_path[len + 11];

    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    // do not want to retry the create since 
    // we would end up creating more than one child
    if (ret != ZOK) {
        LOG_WARN(LOGCALLBACK(zh), "could not create zoo node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    // 2. GET CHILDREN, SHIFT INTERVALS AS NEEDED AND DISCARD THOSE NOT IN INTERVAL
    int ret = 0;
    it_node_t* conflict_intervals = _get_sorted_shifted_relevant_intervals(context, &ret);
    // get children call failed in the helper function
    if (*ret != ZOK) {
        return *ret;
    }

}

static it_node_t* _get_sorted_shifted_relevant_intervals(zk_lock_context_t* context, int* ret) {
    // get all existing interval locks
    struct String_vector all_intervals;
    all_intervals.data = NULL;
    all_intervals.count = 0;
    char interval_path[strlen(context->parent_path) + 10];
    sprintf(interval_path, "%s/interval", context->parent_path);
    *ret = zoo_get_children(zh, interval_path, 0, &all_intervals);
    if (*ret != ZOK) {
        return NULL;
    }

    // get interval tree from mysql for this file

    // discard intervals not relevant to current context
    it_node_t cur_interval = {
        .base = context->base,
        .bound = context->base + context->size - 1,
        .version = context->version,
        .sequence = getSequenceNumber(context->lock_name)
    };

    // return array of relevant intervals sorted by sequence number
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