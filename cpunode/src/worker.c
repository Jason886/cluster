#include "worker_pool.h"
#include "wtk_vipkid_engine.h"

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#include <event2/event.h>
#include <event2/http.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

//extern struct config *g_conf;
extern struct event_base *g_base;
extern wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg;


static struct event *__ev_timer = NULL;
static struct timeval one_sec = { 0, 1000000}; 

static void __timmer_cb(evutil_socket_t fd, short what, void *arg)
{
    //printf("child cb_func called times so far.\n");
    if (!evtimer_pending(__ev_timer, NULL)) {
        event_del(__ev_timer);
    }
    evtimer_add(__ev_timer, &one_sec);
}

void worker_run() {
    worker_pool_t *pool = g_worker_pool;

    usleep(1000);
    printf("worker#%u setup\n", pool->idx);

    event_reinit(g_base);
    cpunode_httpd_free();
    event_base_free(g_base);
    g_base = NULL;
    g_base = event_base_new();
    if (!g_base) {
        sleep(2);
        exit(1);
    }
    __ev_timer = evtimer_new(g_base, __timmer_cb, NULL);
    evtimer_add(__ev_timer, &one_sec);
    event_base_dispatch(g_base);

    event_free(__ev_timer);
    __ev_timer = NULL;

    while(1) {
        /*
        sleep(1);
        continue;
        usleep(10000);
        worker_t *worker = &pool->workers[pool->idx];
        worker->used++;
        if (worker->used >= pool->max_use) {
            printf("worker#%u max used exit\n", pool->idx);
            exit(1);
        }
        */
        break;
    }

    printf("worker#%u exit\n", pool->idx);
    exit(0);
}
