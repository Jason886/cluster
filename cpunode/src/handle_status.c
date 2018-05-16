#include "handle_status.h"
#include "worker_pool.h"
#include "task.h"
#include "cJSON.h"
#include "liblog.h"

#include <event.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>

void cpunode_handle_status(struct evhttp_request *req, void *arg) {
    enum evhttp_cmd_type method;
    const char *content_type;

    method = evhttp_request_get_command(req);

    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json; charset=UTF-8");  
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "keep-alive");

    if (!(method == EVHTTP_REQ_GET)) {
        evhttp_send_reply(req, 405, "", NULL);
        return;
    }

    struct evbuffer *evb = evbuffer_new();
    if (!evb) {
        evhttp_send_reply(req, 500, "", NULL);
        return;
    }

    struct cJSON *j_status = cJSON_CreateObject();
    struct cJSON *j_sysinfo = cJSON_CreateObject();
    struct cJSON *j_workers = cJSON_CreateArray();
    struct cJSON *j_tasks = cJSON_CreateArray();
    if (j_status) {
        if (j_sysinfo) cJSON_AddItemToObject(j_status, "sysinfo", j_sysinfo);
        if (j_workers) cJSON_AddItemToObject(j_sysinfo, "workers", j_workers);
        if (j_tasks) cJSON_AddItemToObject(j_tasks, "tasks", j_tasks);
    }

    int i = 0; 
    char tmp[2048];

    evbuffer_add_printf(evb, "{");

    // workers
    if (g_workers && g_worker_num > 0) {
        evbuffer_add_printf(evb, "\"workers\": [");
        for (i = 1; i <= g_worker_num; ++i) {
            snprintf(tmp, sizeof(tmp), "{"
                        "\"pid\":%d,"
                        "\"listen_at\":%u,"
                        "\"alive\":%d,"
                        "\"busy\":%d"
                    "}",
                    g_workers[i].pid,
                    g_workers[i].listen_at,
                    g_workers[i].alive,
                    g_workers[i].busy);
            evbuffer_add(evb, tmp, strlen(tmp));
            if (i != g_worker_num) {
                evbuffer_add_printf(evb, ",");
            }
        }
        evbuffer_add_printf(evb, "]");
    }

    // tasks
    task_t * task = task_get_head();
    if (task) {
        evbuffer_add_printf(evb, ",\"tasks\": [");
        while (task) {
            snprintf(tmp, sizeof(tmp), "{"
                        "\"uri\":\"%s\","
                        "\"binded\":%d,"
                        "\"bind_worker_idx\":%d,"
                        "\"state\":%d,"
                        "\"is_async\":%d,"
                        "\"appkey\":\"%s\","
                        "\"token\":\"%s\","
                        "\"fileurl\":\"%s\","
                        "\"callback\":\"%s\","
                        "\"datasize\":%u"
                    "}",
                    task->req_path? task->req_path : "",
                    task->binded,
                    task->bind_worker_idx,
                    task->recv_state,
                    task->is_async,
                    task->appkey? task->appkey : "",
                    task->token? task->token : "",
                    task->fileurl ? task->fileurl : "",
                    task->callback ? task->callback : "",
                    task->size
                );
            evbuffer_add(evb, tmp, strlen(tmp));
            task = task_get_next(task);
            if (task) {
                evbuffer_add_printf(evb, ",");
            }
        }
        evbuffer_add_printf(evb, "]");
    }

    evbuffer_add_printf(evb, "}");

    evhttp_send_reply(req, 200, "OK", evb);
    evbuffer_free(evb);
}

