#include "handle_eval.h"
#include "worker_pool.h"

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#include <event2/http_struct.h>

typedef struct cpunode_request {
    const char *boundary;
    struct evhttp_request *ev_req;
} cpunode_request_t;


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
        //handle(_creq);
        free(_creq);
        malloc_trim(0);
    } else {
        loge("un-supported content type:%s\n", content_type);
        return;
    }
}
