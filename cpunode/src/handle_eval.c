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
extern int g_base_worker_port;
extern unsigned int g_worker_num;
extern unsigned int g_worker_id;
extern worker_t *g_workers;

struct event *g_eval_timer = NULL;
struct timeval g_eval_timeval = { 0, 1000000}; 

typedef struct eval_req_params {
    int _has_token:1;
    int _has_appkey:1;
    int _has_secretkey:1;
    int _has_callback:1;
    int _has_fileurl:1;
    int _has_compress:1;
    int _has_isfea:1;
    char token[_PARAM_VALUE_SIZE];
    char appkey[_PARAM_VALUE_SIZE];
    char secretkey[_PARAM_VALUE_SIZE];
    char callback[_PARAM_VALUE_SIZE];
    char fileurl[_PARAM_VALUE_SIZE];
    int compress:1;
    int isfea;
    struct evbuffer *ev_file;
} eval_req_params_t;

static void __dispatch_error_1(struct evhttp_request *req, int errno, const char *errmsg, struct cJSON * j_req, char *token) {
    if (req) {
        if (errmsg == NULL) {
            errmsg = cpunode_errmsg(errno);
        } 

        struct evbuffer *evb = evbuffer_new();
        if (j_req) {
            char * req_text = cJSON_PrintUnformatted(j_req);
            if (token) {
                evbuffer_add_printf(evb, "{\"token\":\"%s\", \"error\":{\"errno\":%d,\"info\":\"%s\"}, \"request\":%s}", token, errno, errmsg, req_text?req_text:"null");
            } else {
                evbuffer_add_printf(evb, "{\"error\":{\"errno\":%d,\"info\":\"%s\"}, \"request\":%s}", errno, errmsg, req_text?req_text:"null");
            }
            free(req_text);
        }
        else {
            if (token) {
                evbuffer_add_printf(evb, "{\"token\":\"%s\",\"error\":{\"errno\":%d,\"info\":\"%s\"}}", token, errno, errmsg);
            } else {
                evbuffer_add_printf(evb, "{\"error\":{\"errno\":%d,\"info\":\"%s\"}}", errno, errmsg);
            }
        }
        evhttp_send_reply(req, 200, "OK", evb);
        //!!! loge的参数不对
        //loge("response error: %*.s\n", EVBUFFER_DATA(evb), EVBUFFER_LENGTH(evb));
        evbuffer_free(evb);
    }
}

static void __dispatch_async_commit(struct evhttp_request *req, struct cJSON *j_req, char *token, char *result) {
    struct evbuffer *evb = evbuffer_new();
    char *req_text = cJSON_PrintUnformatted(j_req);
    if (result) {
        evbuffer_add_printf(evb, "{\"token\":\"%s\", \"data\":%s, \"request\":%s}", token?token:"null", result, req_text?req_text:"null");
    } else {
        evbuffer_add_printf(evb, "{\"token\":\"%s\", \"request\":%s}", token?token:"null", req_text?req_text:"null");
    }
    evhttp_send_reply(req, 200, "OK", evb);
    if (req_text) free(req_text);
    evbuffer_free(evb);
}

static void __dispatch_worker_result() {
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
                    else if(strcmp(param, "isfea") == 0) {
                        req_params->_has_isfea = 1; 
                        req_params->isfea = atoi(value);
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
        __dispatch_error_1(req, errno, NULL, NULL, NULL);
        goto _E;
    }

    j_req = cJSON_CreateObject();
    if (!j_req) {
        __dispatch_error_1(req, 10011, "internal error, handle_eval create j_req", NULL, NULL);
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
    if (req_params._has_isfea) {
        cJSON_AddNumberToObject(j_req, "isfea", req_params.isfea);
    }

    if (!req_params._has_token || strlen(req_params.token) == 0) {
        __dispatch_error_1(req, 10003, NULL, j_req, NULL);
        goto _E;
    }

    if (!req_params._has_appkey || strlen(req_params.appkey) == 0) {
        __dispatch_error_1(req, 10004, NULL, j_req, req_params.token);
        goto _E;
    }
    
    if (!req_params._has_secretkey || strlen(req_params.secretkey) == 0) {
        __dispatch_error_1(req, 10006, NULL, j_req, req_params.token);
        goto _E;
    }

    if (!req_params.ev_file || evbuffer_get_length(req_params.ev_file) == 0) {
        if (!req_params._has_fileurl || strlen(req_params.fileurl) == 0) {
            __dispatch_error_1(req, 10008, NULL, j_req, req_params.token);
            goto _E;
        }
        if (memcmp(req_params.fileurl, "http://", 7) != 0) {
            __dispatch_error_1(req, 10005, NULL, j_req, req_params.token);
            goto _E;
        }
    }

    if (req_params.callback && strlen(req_params.callback) > 0) {
        if (memcmp(req_params.callback, "http://", 7) != 0) {
            __dispatch_error_1(req, 10014, NULL, j_req, req_params.token);
        }
    }

    // !!! auth 10007

    if (req_params.ev_file) {
        data = EVBUFFER_DATA(req_params.ev_file);
        size = EVBUFFER_LENGTH(req_params.ev_file);
    }

    task = task_new(uri, j_req, data, size);
    if (!task) {
        __dispatch_error_1(req, 10011, "internal error, task_new failed", j_req, req_params.token);
        goto _E;
    }

    if (req_params.ev_file) {
        evbuffer_free(req_params.ev_file);
        req_params.ev_file = 0;
    }

    if (task->is_async) {
        task_add_tail(task);
        task_list_dump();
        __dispatch_async_commit(req, j_req, req_params.token, NULL);
    } else {
        __dispatch_error_1(req, 10010, NULL,  j_req, req_params.token);
        goto _E;
    }
    
    return;

_E:
    if (task) task_free(task); task = NULL;
    if (j_req) cJSON_Delete(j_req); j_req = NULL;
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

    logd("cpunode_handle_eval, g_worker_id = %u\n", g_worker_id);
    logi("Received request. uri: %s, method:%d, from: %s:%d\n", evhttp_request_get_uri(req), method, req->remote_host, req->remote_port);

    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/json; charset=UTF-8");  
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "keep-alive");

    if (!((method == EVHTTP_REQ_GET) || (method == EVHTTP_REQ_POST))) {
        loge("un-supported http method: %d\n", method);
        __dispatch_error_1(req, 10001, NULL, NULL, NULL);
        return;
    }

    content_type = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
    if (content_type && strstr(content_type, "multipart/form-data; boundary=")) {
        boundary = content_type + 30;
        __handle(req, boundary);
        return;
    } else {
        loge("un-supported content type: %s\n", content_type);
        __dispatch_error_1(req, 10002, NULL, NULL, NULL);
        return;
    }
}

static void __task_eventcb(struct bufferevent *bev, short events, void *user_data) {
    printf("__task_eventcb\n");
    if (events & BEV_EVENT_EOF) {
        printf("task event eof\n");

    } else if (events & BEV_EVENT_ERROR) {
        printf("task event error\n");
        bufferevent_free(bev);

    } else if (events & BEV_EVENT_CONNECTED) {
        printf(" task event connected\n");
    } else if (events & BEV_EVENT_TIMEOUT) {
        printf("task event timeout\n");
    }

}

static void __task_readcb(struct bufferevent *bev, void *user_data) {
    task_t *task = user_data;
    struct evbuffer *input = bufferevent_get_input(bev);
    u_int32_t result_len;
    int left_n;

    printf("!!!!! 111\n");

    if (!task) {
        loge("__task_readcb got null task\n");
        if (evbuffer_get_length(input)) {
            evbuffer_drain(input, evbuffer_get_length(input));
        }
        return;
    }
    printf("!!!!! 222\n");

    while(evbuffer_get_length(input) > 0) {
        switch (task->recv_state) {
            case 0:
                if (evbuffer_get_length(input) < sizeof(result_len)) {
                    return;
                }
                left_n = sizeof(result_len);
                while (left_n > 0) {
                    int read_n = evbuffer_remove(input, ((char *)&result_len) + sizeof(result_len) - left_n, left_n);
                    if (read_n < 0) {
                        // !!! loge
                        //loge("task#%u evbuffer_remove failed\n", g_worker_id);
                        continue;
                    }
                    left_n -= read_n;
                }
                printf("result_len:%d\n", result_len);
                if (result_len == 0) {
                    // !!! error
                    task->recv_state = -1;
                    return;
                }
                task->result_len = result_len;
                task->recv_state = 1;
                break;
            case 1:
                printf("read_result\n");
                if (evbuffer_get_length(input) < task->result_len) {
                    // error!!!
                    return;
                }
                task->result = malloc(task->result_len+1);

                left_n = task->result_len;
                while (left_n > 0) {
                    int read_n = evbuffer_remove(input, task->result + task->result_len - left_n, left_n);
                    if (read_n < 0) {
                        continue;
                    }
                    left_n -= read_n;
                }
                task->recv_state = 2;

                break;
            case 2:
                printf("!!!!! 555\n");
                goto __RECV_OK;
                break;
            case -1:
                logd("recv done, drain\n");
                evbuffer_drain(input, evbuffer_get_length(input));
                break;
            default:
                break;
        }
    }

__RECV_OK:
    printf("!!!!! 666\n");

    if (task->is_async) {
        printf("read result %s\n", task->result);
        task_remove(task);
    } else {
        // http响应
    }

    task->binded = 0;
    task->bind_worker_idx = 0;
    task_free(task);
}

void eval_timer_cb(evutil_socket_t fd, short what, void *arg) {
    logd("master timer cb\n");

    task_t * task = task_get_head();
    while (task) {
        // !!! 这一段设置为不可中断
        if (!task->binded) {
            int free_idx = get_free_worker();
            if (free_idx > 0) {
                // bind
                task->binded = 1;
                task->bind_worker_idx = free_idx;
                logi("bind task %s to worker#%d\n", task->token, free_idx);

                struct bufferevent *bev = bufferevent_socket_new(g_base, -1, BEV_OPT_CLOSE_ON_FREE);

                bufferevent_setcb(bev, __task_readcb, NULL, __task_eventcb, task);
                bufferevent_enable(bev, EV_READ);
                bufferevent_enable(bev, EV_WRITE);

                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_addr.s_addr = inet_addr("127.0.0.1");
                sin.sin_port = htons(g_base_worker_port + free_idx);

                if (bufferevent_socket_connect(bev, (struct sockaddr*)&sin, sizeof(sin))) {
                    __dispatch_worker_result();
                    //__dispatch_error_1(NULL, 10011, "internal error, task bind failed", NULL, NULL);
                    // unbind
                    // !!!
                    continue;
                }

                // 向管道写

                char *req_text = cJSON_PrintUnformatted(task->j_req);
                u_int32_t req_len = strlen(req_text);
                bufferevent_write(bev, &req_len, sizeof(req_len) );
                bufferevent_write(bev, req_text, req_len);
                free(req_text);

                u_int32_t data_len =  task->data ? task->size : 0;
                bufferevent_write(bev, &data_len, sizeof(data_len));
                if (data_len > 0) {
                    bufferevent_write(bev, task->data, data_len);
                }
            }
        }

        task = task_get_next(task); 
    }

    if (!evtimer_pending(g_eval_timer, NULL)) {
        event_del(g_eval_timer);
    }
    evtimer_add(g_eval_timer, &g_eval_timeval);
}

u_int16_t get_free_worker() {
    printf("get_free_worker\n");
    int i;
    if (!g_workers) return 0;
    for (i = 1; i <= g_worker_num; i++) {
        printf("i = %d\n", i);
        worker_t *worker = &(g_workers[i]);
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
