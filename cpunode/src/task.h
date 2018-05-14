#ifndef _CPUNODE_TASK_H
#define _CPUNODE_TASK_H

#include "cJSON.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bufferevent;

typedef enum {
    e_task_state_read_failed = -1,
    e_task_state_read_len = 0,
    e_task_state_read_result = 1,
    e_task_state_read_done = 2,
} e_task_state_t;

typedef struct task {
    struct task *next;
    struct task *prev;

    char *req_path;
    struct cJSON *j_req;
    char *token;
    char *appkey;
    char *secrekey;
    char *callback;
    char *fileurl;
    char *data;
    size_t size;
    int compress;
    int is_async;

    int binded;
    int bind_worker_idx; // 绑定的工作进程编号 0-未绑定 >0 工作进程编号
    struct bufferevent *bev;

    char *result;
    u_int32_t result_len;
    e_task_state_t recv_state; 
} task_t;

task_t * task_new(const char *req_path, struct cJSON *j_req, char *data, size_t size);

void task_free(task_t *task); 

void task_add_tail(task_t *task);

task_t *task_get_head();

task_t *task_get_next(task_t *task);

void task_remove(task_t *task);

void task_list_dump();

#ifdef __cplusplus
}
#endif


#endif
