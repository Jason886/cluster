#include "task.h"
#include "liblog.h"
#include <string.h>
#include <stdlib.h>

static task_t _head_guard = {0};

task_t * task_new( const char *req_path, struct cJSON *j_req, char *data, size_t size) {

    if (!j_req) {
        return NULL;
    }

    struct cJSON * j_token = cJSON_GetObjectItem(j_req, "token");
    char *token = NULL;
    if (j_token && j_token->type == cJSON_String) {
        token = j_token->valuestring;
    }

    struct cJSON *j_appkey = cJSON_GetObjectItem(j_req, "appkey");
    char *appkey = NULL;
    if (j_appkey && j_appkey->type == cJSON_String) {
        appkey = j_appkey->valuestring;
    }

    struct cJSON *j_secretkey = cJSON_GetObjectItem(j_req, "secretkey");
    char *secretkey = NULL;
    if (j_secretkey && j_secretkey->type == cJSON_String) {
        secretkey = j_secretkey->valuestring;
    }

    struct cJSON *j_callback = cJSON_GetObjectItem(j_req, "callback");
    char *callback = NULL;
    if (j_callback && j_callback->type == cJSON_String) {
        callback = j_callback->valuestring;
    }

    struct cJSON *j_fileurl = cJSON_GetObjectItem(j_req, "fileurl");
    char *fileurl = NULL;
    if (j_fileurl && j_fileurl->type == cJSON_String) {
        fileurl = j_fileurl->valuestring;
    }
 

    char *dup_req_path = NULL;
    if (req_path) {
        dup_req_path = strdup(req_path);
        if (!dup_req_path) {
            return NULL;
        }
    }

    char *dup_data = NULL;
    if (data && size > 0) {
        dup_data = malloc(size);
        if (!dup_data) {
            if (dup_req_path) free(dup_req_path);
            return NULL;
        }
        memcpy(dup_data, data, size);
    }

    task_t * task = malloc(sizeof(*task));
    if (!task) {
        if (dup_req_path) free(dup_req_path);
        if (dup_data) free(dup_data);
        return NULL;
    }
    memset(task, 0, sizeof(*task));

    //int compress;

    task->req_path = dup_req_path;
    task->is_async = 0;
    if (callback && strlen(callback) > 0) {
        task->is_async = 1;
    }

    task->j_req = j_req;
    task->token = token;
    task->appkey = appkey;
    task->secrekey = secretkey;
    task->callback = callback;
    task->fileurl = fileurl;
    task->data = dup_data;
    task->size = size;

    task->binded = 0;
    task->bind_worker_idx = 0;
    //task->bev = NULL;

    task->result = NULL;
    task->result_len = 0;
    task->recv_state = 0;

    return task;
}

void task_free(task_t *task) {
    if (task) {
        if (task->req_path) free(task->req_path); task->req_path = NULL;
        if (task->j_req) cJSON_Delete(task->j_req); task->j_req = NULL;
        if (task->data) free(task->data); task->data = NULL;
        if (task->result) free(task->result); task->result = NULL;
        free(task);
    }
}

void task_add_tail(task_t *task) {
    if (task) {
        if ((&_head_guard)->next == NULL) {
            (&_head_guard)->next = &_head_guard;
            (&_head_guard)->prev = &_head_guard;
        }

        task->next = &_head_guard;
        task->prev = (&_head_guard)->prev;
        (&_head_guard)->prev->next = task;
        (&_head_guard)->prev = task;
    }
}

task_t *task_get_head() {
    if ((&_head_guard)->next && (&_head_guard)->next != &_head_guard) {
        return (&_head_guard)->next;
    }
    return NULL;
}

task_t *task_get_next(task_t *task) {
    if (task) {
        if (task->next != &_head_guard) {
            return task->next;    
        }
    }
    return NULL;
}

void task_remove(task_t *task) {
    if (task) {
        task->prev->next = task->next;
        task->next->prev = task->prev;
    }
}

const char * task_list_dump() {
    static char tmp[81920] = {0};
    size_t pos = 0;

    snprintf(tmp, sizeof(tmp),
            "\n"
            "---------------\n"
            "dump task list:\n"
        );
    pos = strlen(tmp);

    int i = 0;
    task_t *cur = task_get_head();
    while (cur) {
        if (sizeof(tmp) - pos <= 512) {
            snprintf(tmp+pos, sizeof(tmp)-pos, "\n...\n");
            pos = strlen(tmp);
            break;
        }
        snprintf(tmp+pos, sizeof(tmp)-pos, 
                "[%d]\ttoken:%s\n\tcallback:%s\n\tfileurl:%s\n\tbinded:%d\n\tbind_worker_idx:%d\n\tstate:%d\n\n",
                i, cur->token, cur->callback, cur->fileurl, cur->binded, cur->bind_worker_idx, cur->recv_state);
        pos = strlen(tmp);

        cur = task_get_next(cur);
        i++;
    }
    snprintf(tmp+pos, sizeof(tmp)-pos, "---------------\n");
    return tmp;
}
