/*
 * author: xianchen.peng
 * date: 2018-05-03
 * desc: 进程池
 */

#ifndef _PROCESS_POOL_H_
#define _PROCESS_POOL_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct process {
    pid_t pid;
    int pipefd[2];
    u_int16_t used;
} process_t;

typedef struct process_pool {
    u_int16_t process_num; // 子进程数
    u_int16_t process_max_used;
    u_int16_t process_idx; // 子进程idx从1开始, 主进程idx=0 
    struct process *process;
} process_pool_t;

typedef int (*master_f)(process_pool_t *pool); 
typedef int (*worker_f)(process_pool_t *pool);

int process_pool(process_pool_t *pool, u_int16_t process_num, u_int16_t process_max_used, master_f master, worker_f worker);

#ifdef __cplusplus
}
#endif

#endif
