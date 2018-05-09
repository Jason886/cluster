#include "http_download.h"
#include "liblog.h"

#include <event2/event.h>
#include <event2/http.h>
#include <stdlib.h>

typedef struct http_download {
    void (*cb)(int result, char *data, unsigned int size);
    void *user_data;
} http_download_t;

static void __on_connection_close_cb(struct evhttp_connection *connection, void *arg) {
    logd("connection close\n");
    if (arg) {
        free(arg);
    }
}

static void __on_request_cb(struct evhttp_request *request, void *arg) {
    http_download_t *download = NULL;
    int rescode;

    logd("__on_request_cb\n");

    rescode = evhttp_request_get_response_code(request);
    download = arg;

    if (rescode != 200) {
        if (download->cb) {
            download->cb(-1, 0, 0);
        }
    } else {
        struct evbuffer * input = evhttp_request_get_input_buffer(request);
        size_t data_len = evbuffer_get_length(input);
        char *data = malloc(data_len+1);
        evbuffer_remove(input, data, data_len);
        data[data_len] = 0;
        if (download->cb) { // 如果有callback则由callback负责释放data
            download->cb(0, data, data_len);
        } else {
            free(data);
        }
    }

    struct evhttp_connection* connection = evhttp_request_get_connection(request);
    if (connection) {
        evhttp_connection_free(connection);
    }
}

int http_download_start (
        struct event_base *base,
        const char *host,
        unsigned int port,
        const char *uri,
        void (*cb)(int result, char *data, unsigned int size),
        void *user_data
    ) {

    http_download_t *download = NULL;
    struct evhttp_connection *connection = NULL;
    struct evhttp_request *request = NULL;

    if (!cb) {
        loge("no cb\n");
        return -1;
    }

    download = malloc(sizeof(*download));
    if (!download) {
        loge("malloc failed\n");
        return -1;
    }
    memset(download, 0, sizeof(*download));
    download->cb = cb;
    download->user_data = user_data;

    connection = evhttp_connection_base_new(base, NULL, host, port);
    if (!connection) {
        loge("evhttp_connection_base_new failed\n");
        free(download);
        return -1;
    }

    evhttp_connection_set_timeout(connection, 30); // 30s超时
    evhttp_connection_set_retries(connection, 0);
    evhttp_connection_set_max_body_size(connection, 300*1024*1024); // body最大300M
    evhttp_connection_set_closecb(connection, __on_connection_close_cb, download);

    request = evhttp_request_new(__on_request_cb, download);
    if (!request) {
        loge("evhttp_request_new failed\n");
        free(download);
        evhttp_connection_free(connection);
        return -1;
    }
    
    if (evhttp_make_request(
                connection,
                request,
                EVHTTP_REQ_GET,
                uri) ) {
       loge("evhttp_make_request failed\n");
       free(download); 
       evhttp_connection_free(connection);
       return -1;
    };

    return 0;
}
