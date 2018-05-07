#ifndef _CPUNODE_TASK_H
#define _CPUNODE_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct task {
    struct task *next;
    struct task *prev;

    char *req_path;
    char *token;
    char *appkey;
    char *secrekey;
    char *fileurl;
    char *callback;
} task_t;

task_t * task_new(const char *req_path, const char *token, const char *appkey, const char *secrekey, const char *fileurl, const char *callback);
void task_free(task_t *task); 

void task_add_tail(task_t *task);
task_t *task_get_head();
task_t *task_find_first(const char *token);
void task_remove(task_t *task);


#ifdef __cplusplus
}
#endif


#endif
