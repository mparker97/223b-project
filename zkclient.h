#ifndef ZKCLIENT_H
#define ZKCLIENT_H
#include <zookeeper/zookeeper.h>
#include <mysql/mysql.h>
#include "interval_tree.h"
#include "range.h"

#define LOCK_TYPE_INTERVAL 0
#define LOCK_TYPE_MASTER_WRITE 1
#define LOCK_TYPE_MASTER_READ 2

static int zkclient_init(){return 0;}
static int zk_acquire_master_lock(MYSQL* mysql, it_node_t* zkcontext, struct range_file* rf, int lt){return 0;}
static int zk_release_lock(it_node_t* zkcontext){return ZOK;}
static int zk_acquire_lock(MYSQL* mysql, it_node_t *context){return ZOK;};
static int zk_lock_intervals(MYSQL* mysql, struct range_file* rf, int fail_any){return rf->num_it;};
static int zk_unlock_intervals(struct range_file* rf){return rf->num_it;};

#endif