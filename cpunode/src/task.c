#include "task.h"
#include <string.h>
#include <stdlib.h>

static task_t _head_guard = {0};

task_t * task_new(const char *req_path, const char *token, const char *appkey, const char *secrekey, const char *fileurl, const char *callback) {
    char *dup_req_path = strdup(req_path ? req_path : "");
    char *dup_token = strdup(token ? token : "");
    char *dup_appkey = strdup(appkey ? appkey : "");
    char *dup_secretkey = strdup(secrekey ? secrekey : "");
    char *dup_fileurl = strdup(fileurl ? fileurl : "");
    char *dup_callback = strdup(callback ? callback : "");
    if (
            !dup_req_path ||
            !dup_token ||
            !dup_appkey ||
            !dup_secretkey ||
            !dup_fileurl ||
            !dup_callback
        ) {
        if (dup_req_path) free(dup_req_path);
        if (dup_token) free(dup_token);
        if (dup_appkey) free(dup_appkey);
        if (dup_secretkey) free(dup_secretkey);
        if (dup_fileurl) free(dup_fileurl);
        if (dup_callback) free(dup_callback);
        return NULL;
    }

    task_t * task = malloc(sizeof(*task));
    if (task) {
        memset(task, 0, sizeof(*task));
        task->req_path = dup_req_path;
        task->token = dup_token;
        task->appkey = dup_appkey;
        task->secrekey = dup_secretkey;
        task->fileurl = dup_fileurl;
        task->callback = dup_callback;
    }
    return task;
}

void task_free(task_t *task) {
    if (task) {
        free(task->req_path);
        free(task->token);
        free(task->appkey);
        free(task->secrekey);
        free(task->fileurl);
        free(task->callback);
        free(task);
    }
}

void task_add_tail(task_t *task) {
    if (!task) return;
    if ((&_head_guard)->next == NULL) {
        (&_head_guard)->next = &_head_guard;
        (&_head_guard)->prev = &_head_guard;
    }

    task->next = &_head_guard;
    task->prev = (&_head_guard)->prev;
    (&_head_guard)->prev->next = task;
    (&_head_guard)->prev = task;
}

task_t *task_get_head() {
    if ((&_head_guard)->next && (&_head_guard)->next != &_head_guard) {
        return (&_head_guard)->next;
    }
    return NULL;
}

task_t *task_find_first(const char *token) {
    task_t *cur = (&_head_guard)->next;
    while (cur != (&_head_guard)) {
        if (strcmp(cur->token, token) == 0) {
            return cur;
        }
        cur = cur->next;
    }
}

void task_remove(task_t *task) {
    if (task) {
        task->prev->next = task->next;
        task->next->prev = task->prev;
    }
}
