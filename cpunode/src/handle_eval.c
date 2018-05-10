#include "handle_eval.h"
#include "worker_pool.h"
#include "task.h"
#include "err_inf.h"
#include "cJSON.h"

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#include <event.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <jansson.h>

#define _PARAM_NAME_SIZE 128
#define _PARAM_VALUE_SIZE 512
#define _PARAM_SCANF_FMT "%*[^=]=\"%127[^\"]\"; filename=\"%511[^\"]\""

extern struct event_base *g_base;

struct event *g_eval_timer = NULL;
struct timeval g_eval_timeval = { 0, 1000000}; 

typedef struct eval_req_params {
    int _has_token:1;
    int _has_appkey:1;
    int _has_secretkey:1;
    int _has_callback:1;
    int _has_fileurl:1;
    int _has_compress:1;
    char token[_PARAM_VALUE_SIZE];
    char appkey[_PARAM_VALUE_SIZE];
    char secretkey[_PARAM_VALUE_SIZE];
    char callback[_PARAM_VALUE_SIZE];
    char fileurl[_PARAM_VALUE_SIZE];
    int compress:1;
    struct evbuffer *ev_file;
} eval_req_params_t;

static void __dispatch_error_1(struct evhttp_request *req, int errno, const char *errmsg) {
    if (req) {
        if (errmsg == NULL) {
            errmsg = cpunode_errmsg(errno);
        } 
        struct evbuffer *evb = evbuffer_new();
        evbuffer_add_printf(evb, "{\"error\":{\"errno\":%d,\"info\":\"%s\"}}", errno, errmsg);
        // !!! loge
        evhttp_send_reply(req, 200, "OK", evb);
        evbuffer_free(evb);
    }
}

static int __fill_req_params(struct evhttp_request *req, const char *boundary, eval_req_params_t * req_params) {
    char *line;
    int parse_state     = -1; //-1: unknown, 0:boundary, 1: header, 2: body
    int is_binary       = 0;
    char param[_PARAM_NAME_SIZE]      = {0};
    char value[_PARAM_VALUE_SIZE]     = {0};
    char filename[_PARAM_VALUE_SIZE]  = {0};
    struct evbuffer *buf = evhttp_request_get_input_buffer(req);

    while (1) {
        if (!is_binary) {
            line = evbuffer_readline(buf);
            if (!line) break;
            printf("lien: %s\n", line);

            switch (parse_state) {
                case -1:
                    if (strstr(line, boundary)) {
                        parse_state = 0;
                        free(line);
                        continue;
                    }

                    break;
                case 0:
                    memset(param, 0, sizeof(param));
                    memset(filename, 0, sizeof(filename));
                    sscanf(line, _PARAM_SCANF_FMT, param, filename);
                    is_binary = strlen(filename) > 0;
                    parse_state = 1;
                    free(line);
                    logd("[ HEAD ] is_binary: %d, param: %s, filename: %s, state: %d, line: %s\n", is_binary, param, filename, parse_state, line);

                    break;
                case 1:
                    memset(value, 0, sizeof(value));
                    snprintf(value, sizeof(value), "%s", line);
                    free(line);
                    parse_state = -1;
                    logd("[ BODY ] param: %s, value: %s\n", param, value);
                    if(strcmp(param, "token") == 0) {
                        req_params->_has_token = 1;
                        strcpy(req_params->token, value);
                    }
                    else if(strcmp(param, "appkey") == 0) {
                        req_params->_has_appkey = 1;
                        strcpy(req_params->appkey, value);
                    }
                    else if(strcmp(param, "secretkey") == 0) {
                        req_params->_has_secretkey = 1;
                        strcpy(req_params->secretkey, value);
                    }
                    else if(strcmp(param, "callback") == 0) {
                        req_params->_has_callback = 1;
                        strcpy(req_params->callback, value);
                    }
                    else if(strcmp(param, "fileurl") == 0) {
                        req_params->_has_fileurl = 1;
                        strcpy(req_params->fileurl, value);
                    }
                    else if(strcmp(param, "compress") == 0) {
                        req_params->_has_compress = 1;
                        req_params->compress = atoi(value);
                    }

                    break;
            }
        }
        else {
            char *res_type = evbuffer_readline(buf);

            struct evbuffer_ptr p;
            evbuffer_ptr_set(buf, &p, 0, EVBUFFER_PTR_SET);
            p = evbuffer_search(buf, boundary, strlen(boundary), &p);

            req_params->ev_file = evbuffer_new();
            int r = evbuffer_remove_buffer(buf, req_params->ev_file, p.pos - 4);

            logd("[ FILE ] name: %s, file length: %zu, data: %20s\n", filename, EVBUFFER_LENGTH(req_params->ev_file), EVBUFFER_DATA(req_params->ev_file));

            is_binary = 0;
            free(res_type);
            parse_state = -1;
        }
    }
    return 0;
}

static void __handle(struct evhttp_request *req, const char *boundary) {
    eval_req_params_t req_params = {0};
    int errno = 0;
    char *data = NULL;
    size_t size = 0;
    task_t *task = NULL;
    struct cJSON *j_req = NULL;
    const char *uri = evhttp_request_get_uri(req);

    errno = __fill_req_params(req, boundary, &req_params);
    if (errno) {
        goto _E;
    }

    if (!req_params._has_token || strlen(req_params.token) == 0) {
        __dispatch_error_1(req, 10003, NULL);
        goto _E;
    }

    if (!req_params._has_appkey || strlen(req_params.appkey) == 0) {
        __dispatch_error_1(req, 10004, NULL);
        goto _E;
    }
    
    if (!req_params._has_secretkey || strlen(req_params.secretkey) == 0) {
        __dispatch_error_1(req, 10006, NULL);
        goto _E;
    }

    if (!req_params.ev_file || evbuffer_get_length(req_params.ev_file) == 0) {
        if (!req_params._has_fileurl || strlen(req_params.fileurl) == 0) {
            __dispatch_error_1(req, 10008, NULL);
            goto _E;
        }
    }

    // !!! auth 10007

    if (req_params.ev_file) {
        data = EVBUFFER_DATA(req_params.ev_file);
        size = EVBUFFER_LENGTH(req_params.ev_file);
    }

    // create j_req
    j_req = cJSON_CreateObject();
    if (!j_req) {
        __dispatch_error_1(req, 10009, "internal error, handle_eval create j_req");
        goto _E;
    }
    if (req_params._has_token) {
        cJSON_AddStringToObject(j_req, "token", req_params.token);
    }
    if (req_params._has_appkey) {
        cJSON_AddStringToObject(j_req, "appkey", req_params.appkey);
    }
    if (req_params._has_secretkey) {
        cJSON_AddStringToObject(j_req, "secretkey", req_params.secretkey);
    }
    if (req_params._has_callback) {
        cJSON_AddStringToObject(j_req, "callback", req_params.callback);
    }
    if (req_params._has_fileurl) {
        cJSON_AddStringToObject(j_req, "fileurl", req_params.fileurl);
    }
    if (req_params._has_compress) {
        cJSON_AddNumberToObject(j_req, "compress", req_params.compress);
    }

    task = task_new(uri, j_req, data, size);
    if (!task) {
        __dispatch_error_1(req, 10009, "internal error, task_new failed");
        goto _E;
    }

    if (req_params.ev_file) {
        evbuffer_free(req_params.ev_file);
        req_params.ev_file = 0;
    }

    if (task->is_async) {
        task_add_tail(task);
        char *ori_req = cJSON_PrintUnformatted(j_req);
        struct evbuffer *evb = evbuffer_new();
        evbuffer_add_printf(evb, "{\"token\":\"%s\", \"request\":%s}", req_params.token, ori_req?ori_req:"null");
        evhttp_send_reply(req, 200, "OK", evb);
        if (ori_req) free(ori_req);
        evbuffer_free(evb);
    } else {
        __dispatch_error_1(req, 10010, NULL);
        goto _E;
    }
    
    return;

_E:
    if (j_req) {
        cJSON_Delete(j_req);
        j_req = NULL;
    }
    if (req_params.ev_file) {
        evbuffer_free(req_params.ev_file);
        req_params.ev_file = 0;
    }
    return;
}

void cpunode_handle_eval(struct evhttp_request *req, void *arg) {
    enum evhttp_cmd_type method;
    const char *content_type;
    const char *boundary;

    method = evhttp_request_get_command(req);

    logd("cpunode_handle_eval in idx = %u\n", g_worker_pool->idx);
    logi("Received request. uri: %s, method:%d, from: %s:%d\n", evhttp_request_get_uri(req), method, req->remote_host, req->remote_port);

    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/json; charset=UTF-8");  
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "keep-alive");

    if (!((method == EVHTTP_REQ_GET) || (method == EVHTTP_REQ_POST))) {
        loge("un-supported http method: %d\n", method);
        __dispatch_error_1(req, 10001, NULL);
        return;
    }

    content_type = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
    if (content_type && strstr(content_type, "multipart/form-data; boundary=")) {
        boundary = content_type + 30;
        __handle(req, boundary);
        return;
    } else {
        loge("un-supported content type: %s\n", content_type);
        __dispatch_error_1(req, 10002, NULL);
        return;
    }
}

static void __task_readcb(struct bufferevent *bev, void *user_data) {
    task_t *task = user_data;
    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) > 0) {
        int len = evbuffer_get_length(input);
        char *data = malloc(len+1);
        memset(data, 0, len+1);
        evbuffer_remove(input, data, len);
        printf("read result %s\n", data);
        free(data);
    }
    //bufferevent_free(bev);
    task->binded = 0;
    task->bind_worker_idx = 0;

    task_remove(task);
}

void eval_timer_cb(evutil_socket_t fd, short what, void *arg) {

    //printf("cb_func called times so far.\n");

    task_t * task = task_get_head();
    while (task) {
                // 这一段设置为不可中断
                
        if (!task->binded) {
            int free_idx = get_free_worker();
            if (free_idx > 0) {
                // bind
                task->binded = 1;
                task->bind_worker_idx = free_idx;
                logi("bind task %s to worker#%d\n", task->token, free_idx);
                
                //struct bufferevent *bev = g_worker_pool->workers[free_idx].bev;
                struct bufferevent *bev = bufferevent_socket_new(g_base, g_worker_pool->workers[free_idx].pipefd[1], 0);
                if (!bev) {
                    task = task_get_next(task);
                    continue;
                }
                // 清空管道的input

                bufferevent_setcb(bev, __task_readcb, NULL, NULL, task);
                bufferevent_enable(bev, EV_READ);
                bufferevent_enable(bev, EV_WRITE);

                // 向管道写
                bufferevent_write(bev, "\n", 1);
                bufferevent_write(bev, WORKER_FRAME_MAGIC_HEAD, sizeof(WORKER_FRAME_MAGIC_HEAD));
                bufferevent_write(bev, "\n", 1);

                char *ori_req = cJSON_PrintUnformatted(task->j_req);
                bufferevent_write(bev, ori_req, strlen(ori_req));
                free(ori_req);
                bufferevent_write(bev, "\n", 1);
                if (task->data && task->size > 0) {
                    char tmp[128];
                    snprintf(tmp, sizeof(tmp), "%d\n", task->size);
                    bufferevent_write(bev, tmp, strlen(tmp));
                    bufferevent_write(bev, task->data, task->size);
                } else {
                    bufferevent_write(bev, "0\n", 2);
                }
                bufferevent_write(bev, "\n", 1);
            }
        }

        task = task_get_next(task); 
    }

        /*
    while (task && !task_is_end(task)) {
        if (task->state == 1) { // working
            printf("read task result\n");
            if ("read done") {
                printf("remove task\n");
                task_t *rm = task;
                task = task->next;
                task_remove(rm);
                task_free(rm);
                continue;
            }
            // !!!not read done
            task = task->next;
            continue;
        }
    }
        */

    if (!evtimer_pending(g_eval_timer, NULL)) {
        event_del(g_eval_timer);
    }
    evtimer_add(g_eval_timer, &g_eval_timeval);
}

u_int16_t get_free_worker() {
    printf("get_free_worker\n");
    int i;
    if (!g_worker_pool) return 0;
    for (i = 1; i <= g_worker_pool->worker_num; i++) {
        printf("i = %d\n", i);
        worker_t *worker = &(g_worker_pool->workers[i]);
        printf("worker->alive = %d, worker->busy = %d\n", worker->alive, worker->busy);
        if (worker->alive && !worker->busy) {
            int binding = 0;
            printf("binding = %d\n", binding);
            task_t *cur = task_get_head();
            while (cur) {
                printf("cur->token = %s\n", cur->token);
                if (cur->bind_worker_idx == i) {
                    printf("binding = 1 at %d\n", i);
                    binding = 1;
                    break;
                }
                cur = task_get_next(cur);
            }
            if (!binding) {
                return i;
            }
        }
    }
    return 0;
}

    /*
int bind_worker(task_t *task, u_int16_t idx) {
    if (!task) {
        return -1;
    }
    if (idx > g_worker_pool.worker_num) {
        return -1;
    }

}
    */
