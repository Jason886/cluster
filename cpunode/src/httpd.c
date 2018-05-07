#include "global.h"
#include "httpd.h"
#include "handle_eval.h"

static struct {
    char * path;
    void(*cb)(struct evhttp_request *, void *);
} __router[] = {
    {"/eval", cpunode_handle_eval},
    {"/hello", NULL}
};
#define ROUTER_NUM (sizeof(__router)/sizeof(__router[0]))

int cpunode_httpd_init(struct config *conf, int port) {
    struct event_base *base = g_base;
    struct evhttp *http = NULL;
    struct evhttp_bound_socket *bound_socket = NULL;
    int ix = 0;

    http = evhttp_new(base);
    if (!http) {
        loge("Couldn't create evhttp. Exiting.\n");
        goto _E;
    }
    g_httpd.http = http;

    for (ix = 0; ix < ROUTER_NUM; ++ix) {
        if (__router[ix].path && __router[ix].path[0] && __router[ix].cb) {
            evhttp_set_cb(http, __router[ix].path, __router[ix].cb, __router[ix].path);
        }
    }

    char *host = conf_get_string(conf, "cpunode:host");
    if (!host) { 
       host = "0.0.0.0";
       logi("Couldn't get paramter: host in config, using 0.0.0.0 now.\n");
    } 
    
    bound_socket = evhttp_bind_socket_with_handle(http, host, port);
    if (!bound_socket) {
        loge("Couldn't bind to port %d. Exiting.\n", port);
        goto _E;
    }
    g_httpd.socket = bound_socket;

    logi("Listening on %s:%d...\n", host, port);

    return 0;

_E:
    if (http) {
        evhttp_free(http);
    }
    memset(g_httpd, 0, sizeof(g_httpd));
    return -1;
}


void cpunode_httpd_free() {
    if (g_httpd.http) {
        evhttp_free(g_httpd.http);
        g_httpd.http = NULL;
    }
}

