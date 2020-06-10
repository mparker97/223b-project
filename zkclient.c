#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h> 
#include <unistd.h>
#include <pthread.h>
#include "sql.h"
#include "interval_tree.h"
#include "range.h"
#include "zkclient.h"

static zhandle_t *zh;
int zkconnected;
const char hostPorts[] = "34.215.40.55:32181,34.210.144.130:32181,34.219.108.247:32181";
static clientid_t myid;

int zkclient_init() {
    zkconnected = 0;

    // append zk logs to file
    FILE* zklog = fopen("./zklog", "a");
    zoo_set_log_stream(zklog);

    zh = zookeeper_init(hostPorts, watcher, 10000, 0, NULL, 0);
    time_t expires = time(0) + 10;
    while(!zkconnected && time(0) < expires) {
        sleep(1);
    }
    if (!zkconnected) {
        fprintf(stderr, "Zookeeper client failed to init");
    }
    return zkconnected;
}

/**
 * Watcher function that gets called on event trigger
 */
void watcher(zhandle_t *zzh, int type, int state, const char *path,
             void* context) {
    fprintf(stderr, "Watcher %d state = %d", type, state);
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
            zkconnected = 1;
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            fprintf(stderr, "Authentication failure. Shutting down...\n");
            zookeeper_close(zzh);
            zh=0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            fprintf(stderr, "Session expired. Shutting down...\n");
            zookeeper_close(zzh);
            zh=0;
             zkclient_init();
        }
    }
    else if (type == ZOO_DELETED_EVENT) {
        // only master locks are watched
        it_node_t* zkcontext = (it_node_t*) context;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (.5)*1000000;

        // helper function will set watch if not owner
        if (zkcontext->lock_type == LOCK_TYPE_MASTER_READ) {
            _zk_determine_master_read_lock_eligibility(zkcontext, &ts);
        }
        else if (zkcontext->lock_type == LOCK_TYPE_MASTER_WRITE) {
            _zk_determine_master_write_lock_eligibility(zkcontext, &ts);
        }

        // successfully acquired lock
        if (zkcontext->lock_acquired) {
            fprintf(stdout, "successfully acquired lock on delete");
            // matching unlock for lock on client
            pthread_mutex_unlock(&(zkcontext->pmutex));
        } 
    }
}

int zk_acquire_master_lock(it_node_t* zkcontext, struct range_file* rf, int lt){
	// master read/write lock
	zkcontext->lock_type = lt;
	zkcontext->file_path = rf->file_path;
	pthread_mutex_init(&(zkcontext->pmutex), NULL);
	if (zk_acquire_lock(zkcontext) != ZOK){
		return -1;
	}
	if (!zkcontext->lock_acquired) {
		// watcher in zkclient will unlock once lock gets acquired
		pthread_mutex_lock(&(zkcontext->pmutex));
	}
	return 0;
}

int zk_release_lock(it_node_t *context) {
    if (context->lock_name != NULL && context->file_path != NULL) {
        int len = strlen(context->file_path) + strlen(context->lock_name);
        if (context->lock_type == LOCK_TYPE_INTERVAL) {
            // 10 for "/interval/" + 1 for '\0'
            len += 11;
        }
        else {
            // 8 for "/master/" + 1 for '\0'
            len += 9;
        }
        
        char buf[len];
        if (context->lock_type == LOCK_TYPE_INTERVAL) {
            sprintf(buf, "%s/interval/%s", context->file_path, context->lock_name);
        }
        else {
            sprintf(buf, "%s/master/%s", context->file_path, context->lock_name);
        }

        int ret = 0;
        int count = 0;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (.5)*1000000;
        ret = ZCONNECTIONLOSS;
        while (ret == ZCONNECTIONLOSS && (count < 3)) {
            ret = zoo_delete(zh, buf, -1);
            if (ret == ZCONNECTIONLOSS) {
                fprintf(stderr, "connectionloss while deleting the node");
                nanosleep(&ts, 0);
                count++;
            }
        }
        if (ret == ZOK || ret == ZNONODE) {
            return 0;
        }
        fprintf(stderr, "not able to connect to server - giving up");
        return ZCONNECTIONLOSS;
    }

    fprintf(stderr, "either parent path or lock name is NULL");
	return ZSYSTEMERROR;
}

int retry_create(char* znode, struct timespec * ts) {
    struct Stat stat;
    int exists = zoo_exists(zh, znode, 0, &stat);
    int count = 0;
    while ((exists == ZCONNECTIONLOSS || exists == ZNONODE) && (count < 3)) {
        count++;
        // retry the operation
        if (exists == ZCONNECTIONLOSS) 
            exists = zoo_exists(zh, znode, 0, &stat);
        else if (exists == ZNONODE) 
            exists = zoo_create(zh, znode, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
        nanosleep(ts, 0);        
    }

    return exists;
}

int zk_acquire_lock(it_node_t *context) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (.5)*1000000;
    int exists;

    // retry to see if the parent path exists and 
    // and recursively create all subdirectories2 if the parent path does not exist
    // skip the first root directory /
    char* slashptr = context->file_path + 1;
    while ((slashptr = strchr(slashptr, '/')) != NULL) {
        // temporarily change / to \0 
        *slashptr = 0;
        exists = retry_create(context->file_path, &ts);
        if (exists != ZOK) {
            return exists;
        }
        *slashptr = '/';
        slashptr++;
    }
    // for full path 
    exists = retry_create(context->file_path, &ts);
    if (exists != ZOK) {
        return exists;
    }

    int lock_subfolder_len = strlen(context->file_path);
    if (context->lock_type == LOCK_TYPE_INTERVAL) {
        // 9 for "/interval" and 1 for '\0'
        lock_subfolder_len += 10;
    }
    else {
        // 7 for "/master" and 1 for '\0'
        lock_subfolder_len += 8;
    }

    // create subnode .../foo/interval if it doesn’t exist before
    char subfolder_path[lock_subfolder_len];
    if (context->lock_type == LOCK_TYPE_INTERVAL) {
        sprintf(subfolder_path, "%s/interval", context->file_path);
    }
    else {
        sprintf(subfolder_path, "%s/master", context->file_path); 
    }
    exists = retry_create(subfolder_path, &ts);
    if (exists == ZCONNECTIONLOSS) {
        return exists;
    }

    // attempt actual lock creation
    int check_retry = ZCONNECTIONLOSS;
    int count = 0;
    while (check_retry != ZOK && count < 3) {
        if (context->lock_type == LOCK_TYPE_INTERVAL) {
            check_retry = _zk_interval_lock_operation(context, &ts);
        }
        else if (context->lock_type == LOCK_TYPE_MASTER_READ) {
            check_retry = _zk_master_read_lock_operation(context, &ts);
        }
        else if (context->lock_type == LOCK_TYPE_MASTER_WRITE) {
            check_retry = _zk_master_write_lock_operation(context, &ts);
        }

        if (check_retry != ZOK) {
            nanosleep(&ts, 0);
            count++;
        }
    }

    return check_retry;
}

int zk_lock_intervals(struct range_file* rf){
	struct it_node* p_itn;
	int acquired_count = 0;
	it_foreach(&rf->it, p_itn){
		p_itn->file_path = rf->file_path;
		p_itn->lock_type = LOCK_TYPE_INTERVAL;
		if (zk_acquire_lock(p_itn) == ZOK){
			if (p_itn->lock_acquired){
				acquired_count++;
				continue;
			}
		}
		return -1;
	}
	return acquired_count;
}

int zk_unlock_intervals(struct range_file* rf){
	struct it_node* p_itn;
	int release_count = 0;
	it_foreach(&rf->it, p_itn){
		p_itn->file_path = rf->file_path;
		p_itn->lock_type = LOCK_TYPE_INTERVAL;
		if (zk_release_lock(p_itn) == ZOK){
			release_count++;
		}
	}
	return release_count;
}

static int _zk_master_read_lock_operation(it_node_t *context, struct timespec *ts) {
    // 1. CREATE LOCK NODE WITH EPHEMERAL AND SEQUENCE FLAGS ON 
    // construct full lock file path: .../foo/master/read-<SEQUENCE>
    // 14 = 13 for "/master/read-" + 1 for '\0'
    int len = strlen(context->file_path) + 14;
    char buf[len];
    sprintf(buf, "%s/master/read-", context->file_path);

    // 11 for the <SEQUENCE>
    char full_path[len + 11];
    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    if (ret != ZOK) {
       fprintf(stderr, "could not create master read node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    return _zk_determine_master_read_lock_eligibility(context, ts);
}

static int _zk_master_write_lock_operation(it_node_t *context, struct timespec *ts) {
    // 1. CREATE LOCK NODE WITH EPHEMERAL AND SEQUENCE FLAGS ON 
    // construct full lock file path: .../foo/master/read-<SEQUENCE>
    // 15 = 14 for "/master/write-" + 1 for '\0'
    int len = strlen(context->file_path) + 15;
    char buf[len];
    sprintf(buf, "%s/master/write-", context->file_path);

    // 11 for the <SEQUENCE>
    char full_path[len + 11];
    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    if (ret != ZOK) {
       fprintf(stderr, "could not create master write node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    return _zk_determine_master_write_lock_eligibility(context, ts);
}

static int _zk_interval_lock_operation(it_node_t *context, struct timespec *ts) {
    // 1. CREATE LOCK NODE WITH EPHEMERAL AND SEQUENCE FLAGS ON 
    // construct full lock file path: .../foo/interval/<OFFSET_ID>-<SEQUENCE>
    // 23 = 10 for "/interval/" + 1 for '\0' + 1 for '-' + 11 for offset_id number 
    int len = strlen(context->file_path) + 23;
    char buf[len];
    sprintf(buf, "%s/interval/%lu-", context->file_path, context->id);

    // 11 for the <SEQUENCE>
    char full_path[len + 11];
    int ret = zoo_create(zh, buf, NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                         ZOO_EPHEMERAL | ZOO_SEQUENCE, full_path, len + 11);
    if (ret != ZOK) {
       fprintf(stderr, "could not create zoo node %s", buf);
        return ret;
    }
    context->lock_name = getLockName(full_path);

    return _zk_determine_interval_lock_eligibility(context, ts);
}

static int _zk_determine_master_read_lock_eligibility(it_node_t *context, struct timespec *ts) {
    // 2. GET CHILDREN
    // get all existing master locks .../foo/master/<read/write>-<sequence>
    struct String_vector master_locks;
    master_locks.data = NULL;
    master_locks.count = 0;
    char path[strlen(context->file_path) + 8];
    sprintf(path, "%s/master", context->file_path);
    int ret = zoo_get_children(zh, path, 0, &master_locks);
    if (ret != ZOK) {
        return ret;
    }

    // get write locks
    char* znode = strrchr(context->lock_name, '-') + 1; // shouldn't fail at all
    char* endptr;
    int currentSeq = strtol(znode, &endptr, 10);
    int minSeq = currentSeq;
    int nextSmallestSeq = 0;
    char* nextSmallestLock = NULL;

    for (int i=0; i < master_locks.count; i++) {
        znode = strrchr(master_locks.data[i], '/');
        if (znode != NULL && strncmp(znode + 1, "write", 5) == 0) {
            znode = strrchr(znode, '-') + 1;    // shouldn't fail at all
            int s = strtol(znode, &endptr, 10);
            if (s < minSeq) 
                minSeq = s;
            if (s > nextSmallestSeq && s < currentSeq) {
                nextSmallestLock = master_locks.data[i];
                nextSmallestSeq = s;
            }
        }
    }

    // lock acquired if smallest sequence write lock equals currentSequence
    if (currentSeq == minSeq) {
        context->lock_acquired = 1;
        return ZOK;
    }
    context->lock_acquired = 0;

    // watch write node with largest seq number smallest than currentSequence   
    struct Stat stat;
    ret = zoo_wexists(zh, nextSmallestLock, watcher, (void*) context, &stat);
    int retry_count = 0;
    while (ret != ZOK && retry_count < 3) {
        ret = zoo_wexists(zh, nextSmallestLock, watcher, (void*) context, &stat);
        if (ret != ZOK) {
            nanosleep(ts, 0);
            retry_count++;
        }
    }

    return ret;
}

static int _zk_determine_master_write_lock_eligibility(it_node_t *context, struct timespec *ts) {
    // 2. GET CHILDREN
    // get all existing master locks .../foo/master/<read/write>-<sequence>
    struct String_vector master_locks;
    master_locks.data = NULL;
    master_locks.count = 0;
    char path[strlen(context->file_path) + 8];
    sprintf(path, "%s/master", context->file_path);
    int ret = zoo_get_children(zh, path, 0, &master_locks);
    if (ret != ZOK) {
        return ret;
    }

    // get all master r/w locks
    int currentSeq = getSequenceNumber(context->lock_name);
    int minSeq = currentSeq;
    int nextSmallestSeq = 0;
    char* nextSmallestLock = NULL;

    for (int i=0; i < master_locks.count; i++) {
        int s = getSequenceNumber(master_locks.data[i]);
        if (s < minSeq) 
            minSeq = s;
        if (s > nextSmallestSeq && s < currentSeq) {
            nextSmallestLock = master_locks.data[i];
            nextSmallestSeq = s;
        }

    }

    // lock acquired if smallest sequence write lock equals currentSequence
    if (currentSeq == minSeq) {
        context->lock_acquired = 1;
        return ZOK;
    }
    context->lock_acquired = 0;
    
    // watch node with largest seq number smallest than currentSequence
    struct Stat stat;
    ret = zoo_wexists(zh, nextSmallestLock, watcher, (void*) context, &stat);
    int retry_count = 0;
    while (ret != ZOK && retry_count < 3) {
        ret = zoo_wexists(zh, nextSmallestLock, watcher, (void*) context, &stat);
        if (ret != ZOK) {
            nanosleep(ts, 0);
            retry_count++;
        }
    }

    return ret;
}

static int _zk_determine_interval_lock_eligibility(it_node_t *context, struct timespec *ts) {
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
        context->lock_acquired = 1;
        return ZOK;
    }
    
    context->lock_acquired = 0;
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
    int len = strlen(context->file_path) + 34;
    char buf[len];
    sprintf(buf, "%s/interval/%lu-%010d", context->file_path,
            (*found_interval)->id, (*found_interval)->sequence);
    
    struct Stat stat;
    ret = zoo_exists(zh, buf, 0, &stat);
    int retry_count = 0;
    while (ret != ZOK && retry_count < 3) {
        ret = zoo_exists(zh, buf, 0, &stat);
        if (ret != ZOK) {
            nanosleep(ts, 0);
            retry_count++;
        }
    }

    // free relevant intervals
    _free_intervals_array(relevant_intervals.array, relevant_intervals.len);

    return ret;
}

static int _get_sorted_shifted_relevant_intervals(it_node_t* context, it_array_t* ret_array) {
    ret_array->array = NULL;
    ret_array->len = 0;

    // get all existing interval locks <offset_id>-<sequence>
    struct String_vector interval_locks;
    interval_locks.data = NULL;
    interval_locks.count = 0;
    char interval_path[strlen(context->file_path) + 10];
    sprintf(interval_path, "%s/interval", context->file_path);
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
    struct range_file rf;
    it_init(&rf.it);
    ret = query_select_file_intervals(&rf, context->file_path, context->id);
    if (ret == -1) {
        return ret;
    }    

    // get array of relevant intervals
    it_node_t* cur_interval;
    it_node_t** interval_array = NULL;
    int interval_count = 0;
    it_foreach(&rf.it, cur_interval){
        // find corresponding sequence from zk for cur_interval
        // ideally this would be a map, but we'll just do a binary search for the time being
        it_node_t** found_interval = (it_node_t**) bsearch(
            &cur_interval, 
            locks_sorted_by_offset_id, 
            interval_locks.count,
            sizeof(cur_interval),
            cmpOffsetIdFunc
        );
        // iterate backwards until start of intervals with the same offset id
        while (found_interval > locks_sorted_by_offset_id &&
               (*(found_interval-1))->id == cur_interval->id) {
            found_interval--;
        }

        while (found_interval < locks_sorted_by_offset_id + interval_locks.count &&
               (*found_interval)->id == cur_interval->id) {
            interval_count++;
            it_node_t** temp_array = realloc(
                interval_array, 
                interval_count * sizeof(it_node_t*)
            );
            if (temp_array == NULL) {
                return -1;
            }
            interval_array = temp_array;
            interval_array[interval_count-1] = _deep_copy_it_node(cur_interval);
            // malloc failed
            if (interval_array[interval_count-1] == NULL) {
                return -1;
            }
            interval_array[interval_count-1]->sequence = (*found_interval)->sequence;
            found_interval++;
        }   
    }

    // free the locks information with just the offset_id and sequence
    _free_intervals_array(locks_sorted_by_offset_id, interval_locks.count);

    // sort array by sequence number
    qsort(interval_array, interval_count, sizeof(it_node_t*), cmpSequenceFunc);

    // set return value
    ret_array->array = interval_array;
    ret_array->len = interval_count;
	
    return ZOK;
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
    lock_name = strrchr(lock_name, '-');
    if (lock_name == NULL) 
        return 0;

    char* endptr;
    errno = 0;
    size_t ret = strtoul(lock_name+1, &endptr, 10);
    if (errno != 0 || *endptr != 0) 
        return 0;

    return ret;
}

it_node_t* _deep_copy_it_node(it_node_t* original) {
    it_node_t* copy = malloc(sizeof(it_node_t));
    if (copy != NULL) {
         memcpy(copy, original, sizeof(it_node_t));
    }
    return copy;
}

// compare function for it_node_t* elements by sequence
int cmpSequenceFunc(const void * a, const void * b) {
    return ((*(it_node_t**)a)->sequence - (*(it_node_t**)b)->sequence);
}

// compare function for it_node_t* elements by offset
int cmpOffsetIdFunc(const void * a, const void * b) {
    return ((*(it_node_t**)a)->id - (*(it_node_t**)b)->id);
}

// compare function for strings, alphabetical order
int cmpAlphabetical(const void * a, const void * b) {
    return strcmp(*((const char **)a), *((const char **)b));
}

// TODO test
// int main() {
    
//     return 0;
// }