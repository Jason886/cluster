#include "http_callback.h"
#include "liblog.h"

#include <event2/event.h>
#include <event2/http.h>
#include <stdlib.h>

typedef struct http_post {
    void (*cb)(int result, char *data, unsigned int size);
    void *user_data;
} http_post_t;





static void __on_connection_close_cb(struct evhttp_connection *connection, void *arg) {
    logd("connection close\n");

    if (arg) {
        free(arg);
    }
}








static void __on_request_cb(struct evhttp_request *request, void *arg) {
    logd("__on_request_cb\n");

    http_post_t *post = arg;
    const char *uri = evhttp_request_get_uri(request);

    logd("uri %s\n", uri);

    do {
        int rescode = evhttp_request_get_response_code(request);
        if (rescode != 200) {
            loge("http rescode: %d, %s\n", rescode, uri);
            if (post->cb) {
                post->cb(-1, 0, 0); 
            }
            break;
        }

        // rescode == 200

        struct evbuffer *input = evhttp_request_get_input_buffer(request);
        size_t length = evbuffer_get_length(input);
        char *data = malloc(length+1);
        if (!data) {
            loge("malloc failed, %s\n", uri);
            if (post->cb) {
                post->cb(-1, 0, 0);
            }
            break;
        }

        int read_n = evbuffer_remove(input, data, length);
        if (read_n < 0) {
            loge("can't drain the buffer, %s\n", uri);
            if (post->cb) {
                post->cb(-1, 0, 0);
            }
            free(data);
            break;
        }

        data[read_n] = 0;
        if (post->cb) {
            post->cb(0, data, read_n);

        }

        // 回调以后释放data
        free(data);
        break;
    } while(1);
}


int http_post (
        struct event_base *base, 
        const char *url,
        const char *data,
        size_t size,
        void (*cb)(int result, char *data, unsigned int size),
        void *user_data
    ) {

    http_post_t *post = NULL;
    struct evhttp_connection *connection = NULL;
    struct evhttp_request *request = NULL;
    struct evhttp_uri * evuri = NULL;

    if (!cb) {
        loge("null cb, %s\n", url);
        return -1;
    }

    post = malloc(sizeof(*post));
    if (!post) {
        loge("malloc failed, %s\n", url);
        return -1;
    }
    memset(post, 0, sizeof(*post));
    post->cb = cb;
    post->user_data = user_data;

    evuri = evhttp_uri_parse(url);
    if (!evuri) {
        loge("invalid %s\n", url);
        free(post);
        return -1;
    }

    const char *host = evhttp_uri_get_host(evuri);
    int port = evhttp_uri_get_port(evuri);
    if (port <= 0) port = 80;

    connection = evhttp_connection_base_new(base, NULL, host, port);
    if (!connection) {
        loge("evhttp_connection_base_new failed, %s\n", url);
        free(post);
        evhttp_uri_free(evuri);
        return -1;
    }
    evhttp_uri_free(evuri);

    evhttp_connection_set_timeout(connection, 30); // 30s超时
    evhttp_connection_set_retries(connection, 0);
    evhttp_connection_set_max_body_size(connection, 30*1024*1024); // body最大30M
    evhttp_connection_set_closecb(connection, __on_connection_close_cb, post);

    request = evhttp_request_new(__on_request_cb, post);
    if (!request) {
        loge("evhttp_request_new failed, %s\n", url);
        free(post);
        evhttp_connection_free(connection);
        return -1;
    }

    evhttp_add_header(evhttp_request_get_output_headers(request), "Content-Type", "text/json; charset=UTF-8");  
    struct evbuffer *output = evhttp_request_get_output_buffer(request); 
    evbuffer_add(output, data, size);

    if (evhttp_make_request(
                connection,
                request,
                EVHTTP_REQ_POST,
                url)
        ) {
       loge("evhttp_make_request failed, %s\n", url);
       free(post); 
       evhttp_connection_free(connection);
       return -1;
    };

    return 0;
}
