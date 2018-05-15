/*
 * author: xianchen.peng
 * date: 2018-05-03
 * desc: 进程池
 */

#ifndef _CPUNODE_WORKER_POOL_H
#define _CPUNODE_WORKER_POOL_H

#include "libconfig.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct worker {
    pid_t pid;
    u_int16_t listen_at;
    int8_t alive;
    int8_t busy;
    void * task;
} worker_t;

int init_workers(struct config *conf);

extern worker_t *g_workers;
extern unsigned int g_worker_num;
extern unsigned int g_worker_max_use;

#ifdef __cplusplus
}
#endif

#endif
