/*
 * author: xianchen.peng
 * date: 2018-05-03
 * desc: 工作进程池
 */

#ifndef _WORKER_POOL_H_
#define _WORKER_POOL_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct worker {
    pid_t pid;
    int pipefd[2];
} worker_t;

typedef struct worker_pool {
    u_int16_t worker_num; // 工作进程数
    u_int16_t worker_idx; // 工作进程idx从1开始, 主进程idx=0 
    struct worker *workers;
} worker_pool_t;

int worker_pool(worker_pool_t *pool, u_int16_t worker_num) {
    int ret = -1;
    int idx = 0;
    int status = 0;

    if (!pool || worker_num == 0) {

    }
}





#ifdef __cplusplus
}
#endif

#endif
