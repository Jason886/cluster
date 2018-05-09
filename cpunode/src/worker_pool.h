/*
 * author: xianchen.peng
 * date: 2018-05-03
 * desc: 进程池
 */

#ifndef _CPUNODE_WORKER_POOL_H
#define _CPUNODE_WORKER_POOL_H

#include "libconfig.h"
#include <sys/types.h>
#include <event2/util.h>

#define WORKER_FRAME_MAGIC_HEAD "CHIVOX_CPUNODE_WORKER_FRAME_HEAD_359152155\0\0\0\0\0\0\0\0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct worker {
    pid_t pid;
    evutil_socket_t pipefd[2];  // 0-子进程使用 1-父进程使用
    u_int16_t used; // 子进程使用
    int8_t alive;
    int8_t busy;
    void *ud;       // 父进程使用
} worker_t;

typedef struct worker_pool {
    u_int16_t max_use;
    u_int16_t idx; // 子进程idx从1开始, 主进程idx=0 
    u_int16_t worker_num; // 子进程数
    worker_t workers[0];
} worker_pool_t;

extern worker_pool_t *g_worker_pool;

int init_worker_pool(struct config *conf);

u_int16_t get_free_worker();

#ifdef __cplusplus
}
#endif

#endif
