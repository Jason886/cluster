#include "http_download.h"
#include "liblog.h"

#include <event2/event.h>
#include <event2/http.h>
#include <stdlib.h>

#include <event.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>

typedef struct http_download {
    void (*cb)(int result, char *data, unsigned int size);
    void *user_data;
} http_download_t;





static void __on_connection_close_cb(struct evhttp_connection *connection, void *arg) {
    logd("__on_connection_close_cb\n");

    //if (arg) {
    //    free(arg);
    //}
}








static void __on_request_cb(struct evhttp_request *request, void *arg) {
    logd("__on_request_cb\n");

    http_download_t *download = arg;

    do {
        if (!request) {
            loge("__on_request_cb: request null\n");
            if (download->cb) {
                download->cb(-1, 0, 0); 
            }
            break;
        }

        const char *uri = evhttp_request_get_uri(request);
        logd("uri %s\n", uri);

        int rescode = evhttp_request_get_response_code(request);
        if (rescode != 200) {
            struct evbuffer *input = evhttp_request_get_input_buffer(request);
            loge("http rescode: %d, %s\n %.*s\n", rescode, uri, EVBUFFER_LENGTH(input), EVBUFFER_DATA(input));
            if (download->cb) {
                download->cb(-1, 0, 0);
            }
            break;
        }

        // rescode == 200

        struct evbuffer *input = evhttp_request_get_input_buffer(request);
        size_t length = evbuffer_get_length(input);
        char *data = malloc(length+1);
        if (!data) {
            loge("malloc failed, %s\n", uri);
            if (download->cb) {
                download->cb(-1, 0, 0);
            }
            break;
        }

        int read_n = evbuffer_remove(input, data, length);
        if (read_n < 0) {
            loge("can't drain the buffer,%s\n", uri);
            if (download->cb) {
                download->cb(-1, 0, 0);
            }
            free(data);
            break;
        }

        data[read_n] = 0;
        if (download->cb) {
            download->cb(0, data, read_n);
            // 如果有回调，则不释放data,把data拥有权交给回调函数
        }
        else {
            free(data);
        }
        break;
    } while(1);

    if (arg) {
        free(arg);
    }
}


int http_download (
        struct event_base *base,
        const char *url,


        void (*cb)(int result, char *data, unsigned int size),
        void *user_data
    ) {

    http_download_t *download = NULL;
    struct evhttp_connection *connection = NULL;
    struct evhttp_request *request = NULL;
    struct evhttp_uri * evuri = NULL;

    if (!cb) {
        loge("null cb, %s\n", url);
        return -1;
    }

    download = malloc(sizeof(*download));
    if (!download) {
        loge("malloc failed, %s\n", url);
        return -1;
    }
    memset(download, 0, sizeof(*download));
    download->cb = cb;
    download->user_data = user_data;

    evuri = evhttp_uri_parse(url);
    if (!evuri) {
        loge("invalid %s\n", url);
        free(download);
        return -1;
    }

    const char *host = evhttp_uri_get_host(evuri);
    const char *path = evhttp_uri_get_path(evuri);
    if (!host || strlen(host) == 0) {
        loge("no host in url: %s\n", url);
        free(download);
        evhttp_uri_free(evuri);
        return -1;
    }
    if (!path || strlen(path) == 0) {
        loge("no path in url: %s\n", url);
        free(download);
        evhttp_uri_free(evuri);
        return -1;
    }
    int port = evhttp_uri_get_port(evuri);
    if (port <= 0) port = 80;

    char header_host[1024];
    snprintf(header_host, sizeof(header_host), "%s:%d", host, port);
    char header_path[512];
    snprintf(header_path, sizeof(header_path), "%s", path);

    logd("header_host= %s:%d, header_path = %s\n", host, port, header_path);

    connection = evhttp_connection_base_new(base, NULL, host, port);
    if (!connection) {
        loge("evhttp_connection_base_new failed, %s\n", url);
        free(download);
        evhttp_uri_free(evuri);
        return -1;
    }
    evhttp_uri_free(evuri);

    evhttp_connection_set_timeout(connection, 30); // 30s超时
    evhttp_connection_set_retries(connection, 0);
    evhttp_connection_set_max_body_size(connection, 300*1024*1024); // body最大300M
    evhttp_connection_set_closecb(connection, __on_connection_close_cb, download);

    request = evhttp_request_new(__on_request_cb, download);
    if (!request) {
        loge("evhttp_request_new failed, %s\n", url);
        free(download);
        evhttp_connection_free(connection);
        return -1;
    }

    struct evkeyvalq *headers = evhttp_request_get_output_headers(request);
    evhttp_add_header(headers, "Host", header_host);
    
    if (evhttp_make_request(
                connection,
                request,
                EVHTTP_REQ_GET,
                header_path)
        ) {
       loge("evhttp_make_request failed, %s\n", url);
       free(download); 
       evhttp_connection_free(connection);
       return -1;
    };

    return 0;
}
