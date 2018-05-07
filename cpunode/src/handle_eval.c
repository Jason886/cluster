#include "handle_eval.h"
#include "worker_pool.h"

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

typedef struct cpunode_request {
    const char *boundary;
    struct evhttp_request *ev_req;
} cpunode_request_t;

#define _PARAM_NAME_SIZE 128
#define _PARAM_VALUE_SIZE 512

struct request_params {
    int compress;
    char filename[_PARAM_VALUE_SIZE];
    char token[_PARAM_VALUE_SIZE];
    char callback[_PARAM_VALUE_SIZE];
};

static void __handle(cpunode_request_t *creq) {
    char *line;
    const char *fmt     = "%*[^=]=\"%[^\"]\"; filename=\"%[^\"]\"";
    int parse_state     = -1; //-1: unknown, 0:boundary, 1: header, 2: body
    int is_binary       = 0;
    char param[_PARAM_NAME_SIZE]      = {0};
    char value[_PARAM_VALUE_SIZE]     = {0};
    struct request_params inputs = {0};

    struct evhttp_request *req = creq->ev_req;
    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    struct evbuffer *ev_file = 0;

    while (1) {
        if (!is_binary) {
            line = evbuffer_readline(buf);
            if (!line) break;

            switch (parse_state) {
                case -1:
                    if (strstr(line, creq->boundary)) {
                        parse_state = 0;
                        free(line);
                        continue;
                    }

                    break;
                case 0:
                    bzero(param, sizeof(param));
                    bzero(inputs.filename, sizeof(inputs.filename));
                    sscanf(line, fmt, param, inputs.filename);
                    is_binary = (strlen(inputs.filename) > 0);
                    parse_state = 1;
                    free(line);
                    //logd("[ HEAD ] is_binary: %d, param: %s, filename: %s, state: %d, line: %s\n", is_binary, param, filename, parse_state, line);

                    break;
                case 1:
                    bzero(value, sizeof(value));
                    strcpy(value, line);
                    free(line);
                    parse_state = -1;
                    logd("[ BODY ] param: %s, value: %s\n", param, value);
                    if(strcmp(param, "compress") == 0) {
                        inputs.compress = atoi(value);
                    }
                    else if(strcmp(param, "callback") == 0) {
                        strcpy(inputs.callback, value);
                    }
                    else if(strcmp(param, "token") == 0) {
                        strcpy(inputs.token, value);
                    }

                    break;
            }
        } else {
            char *res_type;
            res_type = evbuffer_readline(buf);

            struct evbuffer_ptr p;
            evbuffer_ptr_set(buf, &p, 0, EVBUFFER_PTR_SET);
            p = evbuffer_search(buf, creq->boundary, strlen(creq->boundary), &p);

            ev_file = evbuffer_new();
            int r = evbuffer_remove_buffer(buf, ev_file, p.pos - 4);

            logi("[ FILE ] name: %s, file length: %zu, data: %20s\n", inputs.filename, EVBUFFER_LENGTH(ev_file), EVBUFFER_DATA(ev_file));

            is_binary = 0;
            free(res_type);
            parse_state = -1;
        }
    }
    
    json_t *res = 0;
    // 如果是同步任务，判断并发数，如果未超并发，则判断是否有空闲进程，如果有空闲进程，写入工作进程。
    // 如果是异步任务，写入队列
    // IO进程上的定时器从队列中取请求，判断并发数，写入工作进程。
    //_do_analyze(&inputs, ev_file, &res);

    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/json; charset=UTF-8");  
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "keep-alive");

    struct evbuffer *evb = evbuffer_new();
    evbuffer_add_printf(evb, "%s", json_dumps(res, 0));
    evhttp_send_reply(req, 200, "OK", evb);
    evbuffer_free(evb);

    json_decref(res);

}


void cpunode_handle_eval(struct evhttp_request *req, void *path) {
    const char *content_type;
    enum evhttp_cmd_type method = evhttp_request_get_command(req);
    logi("Received request. uri: %s, method:%d, from: %s:%d\n", evhttp_request_get_uri(req), method, req->remote_host, req->remote_port);
    logd("handle_eval in idx = %u\n", g_worker_pool->idx);

    if (!((method == EVHTTP_REQ_GET) || (method == EVHTTP_REQ_POST))) {
        loge("un-supported method: %d\n", method);
        return;
    }

    content_type = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
    if (content_type && strstr(content_type, "multipart/form-data; boundary=")) {
        cpunode_request_t *_creq = CALLOC(1, struct cpunode_request);
        _creq->ev_req = req;
        _creq->boundary = content_type + 30;
        __handle(_creq);
        free(_creq);
        malloc_trim(0);
    } else {
        loge("un-supported content type:%s\n", content_type);
        return;
    }
}
