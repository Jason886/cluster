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
extern unsigned int g_worker_num;
extern unsigned int g_worker_id;
extern worker_t *g_workers;

struct event *g_eval_timer = NULL;
struct timeval g_eval_timeval = { 0, 100000}; 

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
                    //logd("[ HEAD ] is_binary: %d, param: %s, filename: %s, state: %d, line: %s\n", is_binary, param, filename, parse_state, line);

                    break;
                case 1:
                    memset(value, 0, sizeof(value));
                    snprintf(value, sizeof(value), "%s", line);
                    free(line);
                    parse_state = -1;
                    //logd("[ BODY ] param: %s, value: %s\n", param, value);
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

            //logd("[ FILE ] name: %s, file length: %zu, data: %20s\n", filename, EVBUFFER_LENGTH(req_params->ev_file), EVBUFFER_DATA(req_params->ev_file));

            is_binary = 0;
            free(res_type);
            parse_state = -1;
        }
    }
    return 0;
}

static void __req_params_to_json(eval_req_params_t *req_params, struct cJSON *j_req) {
    if (!req_params) return;
    if (!j_req) return;

    if (req_params->_has_token) {
        cJSON_AddStringToObject(j_req, "token", req_params->token);
    }
    if (req_params->_has_appkey) {
        cJSON_AddStringToObject(j_req, "appkey", req_params->appkey);
    }
    if (req_params->_has_secretkey) {
        cJSON_AddStringToObject(j_req, "secretkey", req_params->secretkey);
    }
    if (req_params->_has_callback) {
        cJSON_AddStringToObject(j_req, "callback", req_params->callback);
    }
    if (req_params->_has_fileurl) {
        cJSON_AddStringToObject(j_req, "fileurl", req_params->fileurl);
    }
    if (req_params->_has_compress) {
        cJSON_AddNumberToObject(j_req, "compress", req_params->compress);
    }
    if (req_params->_has_isfea) {
        cJSON_AddNumberToObject(j_req, "isfea", req_params->isfea);
    }
}

static void __dispatch_error_1(struct evhttp_request *req, int errno, const char *errmsg, struct cJSON * j_req, char *token) {
    char *req_text = NULL;
    if (req) {
        if (errmsg == NULL) errmsg = cpunode_errmsg(errno);
        if (j_req) req_text = cJSON_PrintUnformatted(j_req);

        struct evbuffer *evb = evbuffer_new();
        if (!evb) {
            loge("evbuffer_new failed\n");
            loge("response 500, token = %s, request = %s\n", token, req_text);
            evhttp_send_reply(req, 500, "", NULL);
            if (req_text) free(req_text); 
            return;
        }

        if (evbuffer_add_printf(evb, "{"
                "\"token\":%s%s%s,"
                "\"error\":{\"errno\":%d,\"info\":\"%s\"},"
                "\"request\":%s"
            "}",
            token?"\"":"",
            token?token:"null",
            token?"\"":"",
            errno, errmsg,
            req_text?req_text:"null") < 0) {

            loge("evbuffer_add_printf failed\n");
            loge("response 500, token = %s, request = %s\n", token, req_text);
            evhttp_send_reply(req, 500, "", NULL);
            evbuffer_free(evb);
            if (req_text) free(req_text); 
            return;
        }

        logi("response 200: %.*s\n", EVBUFFER_LENGTH(evb), EVBUFFER_DATA(evb));
        evhttp_send_reply(req, 200, "OK", evb);
        evbuffer_free(evb);
        if (req_text) free(req_text);
    }
}

static int __response_task_commit(struct evhttp_request *req, struct cJSON *j_req, char *token) {
    char *req_text = NULL;
    if (j_req) req_text = cJSON_PrintUnformatted(j_req);

    struct evbuffer *evb = evbuffer_new();
    if (!evb) {
        loge("evbuffer_new failed\n");
        loge("response 500, token = %s, request = %s\n", token, req_text);
        evhttp_send_reply(req, 500, "", NULL);
        if (req_text) free(req_text); 
        return -1;
    }

    if (evbuffer_add_printf(evb, "{\"token\":%s%s%s, \"request\":%s}",
            token?"\"":"",
            token?token:"null",
            token?"\"":"",
            req_text?req_text:"null")  < 0) {
        loge("evbuffer_add_printf failed\n");
        loge("response 500, token = %s, request = %s\n", token, req_text);
        evhttp_send_reply(req, 500, "", NULL);
        if (req_text) free(req_text); 
        evbuffer_free(evb);
        return -1;
    }

    logi("response 200: %.*s\n", EVBUFFER_LENGTH(evb), EVBUFFER_DATA(evb));
    evhttp_send_reply(req, 200, "OK", evb);
    evbuffer_free(evb);
    if (req_text) free(req_text);
    return 0;
}

static void __make_task_error_result(task_t *task, int errno, const char *info) {
    if (!task) return;
    const char *fmt = "{"
            "\"token\":%s%s%s,"
            "\"error\":{\"errno\":%d,\"info\":\"%s\"},"
            "\"request\":%s"
        "}";

    if (!info) info = cpunode_errmsg(errno);

    char *req_text = NULL;
    if (task->j_req) req_text = cJSON_PrintUnformatted(task->j_req);

    if (task->result) free(task->result);
    task->result_len = 0;

    size_t len = strlen(fmt) + (task->token?strlen(task->token):4) + (req_text?strlen(req_text):4) + strlen(info) + 256; 
    task->result = malloc(len);
    if (!task->result) {
        if (req_text) free(req_text);
        return;
    }
    memset(task->result, 0, len);
    snprintf(task->result, len, fmt, 
        task->token?"\"":"",
        task->token?task->token:"null",
        task->token?"\"":"",
        errno, info, 
        req_text?req_text:"null"
    );
    task->result_len = strlen(task->result);
    if (req_text) free(req_text);
}

static void __result_callback_cb(int result, char *data, unsigned int size, void *user_data) {

    task_t *task = user_data;

    printf("__result_callback_cb result = %d\n", result);

    if (result != 0) { 
        task->recv_state = e_task_state_failed;
        return;
    }

    if (!data || size == 0) {
        printf("__result_callback_cb data empty\n");
        task->recv_state = e_task_state_failed;
        return;
    }

    char *data_dup = malloc(size +1);
    if (!data_dup) {
        printf("data dup failed\n");
        task->recv_state = e_task_state_failed;
        return;
    }
    memset(data_dup, 0, size+1);
    memcpy(data_dup, data, size);

    struct cJSON *j_data = cJSON_Parse(data_dup);
    if (!j_data) {
        printf("__result_callback_cb data not json: %s\n", data_dup);
        task->recv_state = e_task_state_failed;
        free(data_dup);
        return;
    }

    struct cJSON *j_result = cJSON_GetObjectItem(j_data, "result");
    if (j_result && (
               (j_result->type == cJSON_Number && j_result->valueint == 0) ||
               (j_result->type == cJSON_String && strcmp(j_result->valuestring, "0") == 0) 
                )
            ) {
        printf("__result_callback_cb data ok: %s\n", data_dup);
        task_remove(task);
        task_free(task);
    }
    else {
        printf("__result_callback_cb data error: %s\n", data_dup);
        task->recv_state = e_task_state_failed;
    }
    cJSON_Delete(j_data);
    free(data_dup);
}

static void __response_task_result(task_t *task) {
    if (task->is_async) {
        if (!task->result || task->result_len == 0) {
            task->recv_state = e_task_state_failed;
            return;
        }

        if (http_post (
                    g_base,
                    task->callback,
                    task->result,
                    task->result_len,
                    __result_callback_cb,
                    task
                    ) ) {
            loge("task %s callback failed\n", task->token);
            task->recv_state = e_task_state_failed;
        }
    }
    else {
        // http响应
        task_remove(task);
        task_free(task);
    }
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

    logi("request: token %s\n", req_params.token);

    j_req = cJSON_CreateObject();
    if (!j_req) {
        loge("cJSON_CreateObject failed\n");
        __dispatch_error_1(req, 10011, "internal error, create request json failed", NULL, req_params.token);
        goto _E;
    }

    __req_params_to_json(&req_params, j_req);

    if (!req_params._has_token || strlen(req_params.token) == 0) {
        __dispatch_error_1(req, 10003, NULL, j_req, req_params.token);
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
            goto _E;
        }
    }

    // !!! auth 10007 需要增加auth的代码 及并发控制的代码

    if (req_params.ev_file) {
        data = EVBUFFER_DATA(req_params.ev_file);
        size = EVBUFFER_LENGTH(req_params.ev_file);
    }

    task = task_new(uri, j_req, data, size);
    if (!task) {
        __dispatch_error_1(req, 10011, "internal error, create task failed", j_req, req_params.token);
        goto _E;
    }
    j_req = NULL; // j_req 交由task管理

    if (req_params.ev_file) {
        evbuffer_free(req_params.ev_file);
        req_params.ev_file = 0;
    }

    if (task->is_async) {
        if (__response_task_commit(req, task->j_req, req_params.token)) {
            goto _E;
        }
        task_add_tail(task);
        //printf(task_list_dump());
    } else {
        __dispatch_error_1(req, 10010, NULL,  task->j_req, req_params.token);
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

    //logd("cpunode_handle_eval, g_worker_id = %u\n", g_worker_id);
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
    task_t *task = user_data;
    if (!task) {
        return;
    }

    if (events & BEV_EVENT_EOF) {
        // unbind
        task->bind_worker_idx = 0;
        task->binded = 0;
        bufferevent_free(bev);

        if (task->recv_state != e_task_state_response) {
            loge("task got BEV_EVENT_EOF, token:%s\n", task->token);
            __make_task_error_result(task, 10011, "internal error, task got bev_event_eof");
            task->recv_state = e_task_state_response;
            __response_task_result(task);
        }
        return;

    } else if (events & BEV_EVENT_ERROR) {
        loge("task got BEV_EVENT_ERROR, token:%s\n", task->token);
        // unbind
        task->bind_worker_idx = 0;
        task->binded = 0;
        bufferevent_free(bev);
        if (task->recv_state != e_task_state_response) {
            loge("task got BEV_EVENT_ERROR, token:%s\n", task->token);
            __make_task_error_result(task, 10011, "internal error, task got bev_event_error");
            task->recv_state = e_task_state_response;
            __response_task_result(task);
        }
        return;
    } else if (events & BEV_EVENT_CONNECTED) {
        logd("task event connected\n");
    }
}

static int __read_buffer(struct evbuffer *buffer, char *data, size_t size) {
    size_t left_n = size;
    while (left_n > 0) {
        int read_n = evbuffer_remove(buffer, data + size - left_n, left_n);
        if (read_n < 0) {
            return -1;
        }
        left_n -= read_n;
    }
    return 0;
}

static void __task_readcb(struct bufferevent *bev, void *user_data) {
    struct evbuffer *input = bufferevent_get_input(bev);
    task_t *task = user_data;
    u_int32_t result_len;

    if (!task) {
        loge("__task_readcb got null task\n");
        if (evbuffer_get_length(input)) {
            evbuffer_drain(input, evbuffer_get_length(input));
        }
        return;
    }

    while (evbuffer_get_length(input) > 0) {
        switch (task->recv_state) {
            case e_task_state_wait:
                printf("e_task_state_wait\n");
                return;

            case e_task_state_read_len:
                if (evbuffer_get_length(input) < sizeof(result_len)) {
                    return;
                }

                if (__read_buffer(input, (char *)&result_len, sizeof(result_len))) {
                    loge("task %s __read_buffer failed\n", task->token);
                    __make_task_error_result(task, 10011, "internal error, task __read_buffer failed");
                    task->recv_state = e_task_state_response;
                    goto _RESPONSE;
                }

                if (result_len == 0) {
                    loge("task %s result_len: 0\n", task->token);
                    __make_task_error_result(task, 10011, "internal error, task read result len:0");
                    task->recv_state = e_task_state_response;
                    goto _RESPONSE;
                }

                task->result_len = result_len;
                if (task->result) free(task->result);
                task->result = malloc(result_len+1);
                if (!task->result) {
                    loge("task %s malloc result failed\n", task->token);
                    __make_task_error_result(task, 10011, "internal error, task malloc result failed");
                    task->recv_state = e_task_state_response;
                    goto _RESPONSE;
                }
                memset(task->result, 0, result_len+1);
                task->recv_state = e_task_state_read_result;
                break;

            case e_task_state_read_result:
                if (evbuffer_get_length(input) < task->result_len) {
                    return;
                }

                if (__read_buffer(input, task->result, task->result_len)) {
                    loge("task %s __read_buffer failed\n", task->token);
                    __make_task_error_result(task, 10011, "internal error, task read result failed");
                    task->recv_state = e_task_state_response;
                    goto _RESPONSE;
                }

                task->recv_state = e_task_state_response;
                goto _RESPONSE;

            case e_task_state_response:
                evbuffer_drain(input, evbuffer_get_length(input));
                break;

            case e_task_state_failed:
                loge("task %s in failed state: %d\n", task->token, task->recv_state);
                break;

            default:
                loge("task %s in wrong state: %d\n", task->token, task->recv_state);
                break;
        }
    }

_RESPONSE:
    printf("response: %s\n", task->result);

    // unbind
    task->binded = 0;
    task->bind_worker_idx = 0;
    bufferevent_free(bev);

    //task->bev = NULL;

    __response_task_result(task);
}

void eval_timer_cb(evutil_socket_t fd, short what, void *arg) {
    //logd("master timer cb\n");

    task_t * task = task_get_head();
    while (task) {
        // !!! get_free_worker设置为不可中断
        u_int16_t free_idx = get_free_worker();

        //logd("get_free_worker: %u\n", free_idx);
        if (free_idx == 0) {
            break;
        }
        
        if (task->binded) {
            goto _NEXT;
        }

        if (task->recv_state != e_task_state_wait) {
            goto _NEXT;
        }

        // bind
        task->binded = 1;
        task->bind_worker_idx = free_idx;
        logi("bind task %s to worker#%u\n", task->token, free_idx);

        struct bufferevent *bev = bufferevent_socket_new(g_base, -1, BEV_OPT_CLOSE_ON_FREE);
        if (!bev) {
            loge("bind task %s to worker#%u failed, bufferevent_socket_new failed\n", task->token, free_idx);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }

        //task->bev = bev;

        // !!! 在listened事件中写代码
        bufferevent_setcb(bev, __task_readcb, NULL, __task_eventcb, task);
        bufferevent_enable(bev, EV_READ);
        bufferevent_enable(bev, EV_WRITE);

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        sin.sin_port = htons(g_workers[free_idx].listen_at);

        if (bufferevent_socket_connect(bev, (struct sockaddr*)&sin, sizeof(sin))) {
            loge("bind task %s to worker#%u failed, bufferevent_socket_connect failed\n", task->token, free_idx);
            bufferevent_free(bev);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }

        char *cmd = cJSON_PrintUnformatted(task->j_req);
        if (!cmd) {
            loge("bind task %s to worker#%u failed, print cmd failed\n",task->token, free_idx);
            bufferevent_free(bev);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }

        u_int32_t req_len = strlen(cmd);
        if (bufferevent_write(bev, &req_len, sizeof(req_len))) {
            loge("bind task %s to worker#%u failed, write cmd_len failed\n", task->token, free_idx);
            bufferevent_free(bev);
            free(cmd);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }

        if (bufferevent_write(bev, cmd, req_len)) {
            loge("bind task %s to worker#%u failed, write cmd failed\n", task->token, free_idx);
            bufferevent_free(bev);
            free(cmd);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }
        free(cmd); cmd = NULL;

        u_int32_t data_len =  task->data ? task->size : 0;
        if (bufferevent_write(bev, &data_len, sizeof(data_len))) {
            loge("bind task %s to worker#%u failed, write data_len failed\n", task->token, free_idx);
            bufferevent_free(bev);
            task->bind_worker_idx = 0;
            task->binded = 0;
            goto _NEXT;
        }

        if (data_len > 0) {
            if (bufferevent_write(bev, task->data, data_len)) {
                loge("bind task %s to worker#%u failed, write data failed\n", task->token, free_idx);
                bufferevent_free(bev);
                task->bind_worker_idx = 0;
                task->binded = 0;
                goto _NEXT;
            }
        }
        task->result_len = 0;
        if (task->result) free(task->result);
        task->result = NULL;
        task->recv_state = e_task_state_read_len;

_NEXT:
        task = task_get_next(task); 
    }

    if (!evtimer_pending(g_eval_timer, NULL)) {
        event_del(g_eval_timer);
    }
    evtimer_add(g_eval_timer, &g_eval_timeval);
}

u_int16_t get_free_worker() {
    if (!g_workers) return 0;
    int i;
    for (i = 1; i <= g_worker_num; i++) {
        worker_t *worker = &(g_workers[i]);
        if (worker->alive && !worker->busy) {
            int binding = 0;
            task_t *cur = task_get_head();
            while (cur) {
                if (cur->bind_worker_idx == i) {
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
