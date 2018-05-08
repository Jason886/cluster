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

static void __dispatch_error_1(struct evhttp_request *req, int errno) {
    if (req) {
        const char *errmsg = cpunode_errmsg(errno);
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
    int errno;

    eval_req_params_t req_params = {0};

    errno = __fill_req_params(req, boundary, &req_params);
    if (errno) {
        if (req_params.ev_file) {
            evbuffer_free(req_params.ev_file);
            req_params.ev_file = 0;
        }
        __dispatch_error_1(req, errno);
        return;
    }

    printf("has_token:%d\n", req_params._has_token);
    printf("has_appkey:%d\n", req_params._has_appkey);
    printf("has_secretkey:%d\n", req_params._has_secretkey);
    printf("has_callback:%d\n", req_params._has_callback);
    printf("has_fileurl:%d\n", req_params._has_fileurl);
    printf("has_compress:%d\n", req_params._has_compress);
    printf("token = [%s]\n", req_params.token);
    printf("appkey:%s\n", req_params.appkey);
    printf("secretkey:%s\n", req_params.secretkey);
    printf("callback:%s\n", req_params.callback);
    printf("fileurl:%s\n", req_params.fileurl);
    printf("compress:%d\n", req_params.compress);
    printf("ev_file: %p\n", req_params.ev_file);

    // !!! need trim
    if (!req_params._has_token || strlen(req_params.token) == 0) {
        __dispatch_error_1(req, 10003);
        return;
    }
    // !!! need trim
    if (!req_params._has_appkey || strlen(req_params.appkey) == 0) {
        __dispatch_error_1(req, 10004);
        return;
    }
    // !!! invalid appkey 10005
    
    if (!req_params._has_secretkey || strlen(req_params.secretkey) == 0) {
        __dispatch_error_1(req, 10006);
        return;
    }

    // !!! auth 10007
    
    if (req_params._has_callback && strlen(req_params.callback) > 0) {
        if (req_params.ev_file) {
            if (evbuffer_get_length(req_params.ev_file) == 0) {
                __dispatch_error_1(req, 10008);
                return;
            }

            __dispatch_error_1(req, 20001);
            return;
        }

        if (!req_params._has_fileurl || strlen(req_params.fileurl) == 0) {
            __dispatch_error_1(req, 10009);
            return;
        }

        const char *uri = evhttp_request_get_uri(req);
        task_t *task = task_new(uri, req_params.token, req_params.appkey, req_params.secretkey, req_params.fileurl, req_params.callback);
        if (!task) {
            __dispatch_error_1(req, 10010);
            return;
        }
        task_add_tail(task);

        struct evbuffer *evb = evbuffer_new();
        evbuffer_add_printf(evb, "{\"token\":\"%s\"}", req_params.token);
        evhttp_send_reply(req, 200, "OK", evb);
        evbuffer_free(evb);
        return;

    } else {
        __dispatch_error_1(req, 20001);
        return;
    }
    
    // 如果是同步任务，判断并发数，如果未超并发，则判断是否有空闲进程，如果有空闲进程，写入工作进程。
    // 如果是异步任务，写入队列
    // IO进程上的定时器从队列中取请求，判断并发数，写入工作进程。
    //_do_analyze(&inputs, ev_file, &res);

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
        __dispatch_error_1(req, 10001);
        return;
    }

    content_type = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
    if (content_type && strstr(content_type, "multipart/form-data; boundary=")) {
        boundary = content_type + 30;
        __handle(req, boundary);
        return;
    } else {
        loge("un-supported content type: %s\n", content_type);
        __dispatch_error_1(req, 10002);
        return;
    }
}
