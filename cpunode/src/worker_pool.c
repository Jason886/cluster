#include "liblog.h"
#include "libconfig.h"
#include "worker_pool.h"
#include "worker.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct event_base *g_base;
extern int g_base_worker_port;

unsigned int g_worker_num = 0;
unsigned int g_worker_max_use = 400;
worker_t *g_workers = NULL;

static int __fork_worker(unsigned int id) {
    unsigned short listen_at = g_base_worker_port + id;
    g_workers[id].listen_at = listen_at; 
    g_workers[id].alive = 0;
    g_workers[id].busy = 0;

    struct event_base * worker_base = event_base_new();
    if (!worker_base) {
        loge("event_base_new for worker#%u failed\n", id);
        return -1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(listen_at);

    struct evconnlistener * listener = evconnlistener_new_bind(
            worker_base,
            worker_listener_cb, 
            NULL, 
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, 
            -1, 
            (struct sockaddr*)&sin, 
            sizeof(sin)
            );
    if (!listener) {
        loge("evconnlistener_new_bind for worker#%u failed, listen_at: %u\n", id, listen_at);
        event_base_free(worker_base);
        return -1;
    }
    logi("worker#%u listen at %u\n", id, g_workers[id].listen_at);

    evconnlistener_set_error_cb(listener, worker_listener_error_cb);

    pid_t pid = g_workers[id].pid = fork();
    if (pid < 0) {
        loge("worker#%u fork failed\n", id);
        evconnlistener_free(listener);
        event_base_free(worker_base);

        return -1;
    }
    if (pid > 0) {
        // father
        logi("worker#%u fork ok, pid: %d\n", id, pid);
        evconnlistener_free(listener);
        event_base_free(worker_base);
        g_workers[id].alive = 1;

        return 0;
    }
    else {
        // child
        if (event_reinit(g_base)) {
            loge("worker#%u event_reinit master_base failed\n", id);
            sleep(3);
            exit(1);
        }
        cpunode_httpd_free();
        event_base_free(g_base);
        g_base = NULL;

        if (event_reinit(worker_base)) {
            loge("worker#%u event_reinit worker_base failed\n", id);
            sleep(3);
            exit(1);
        }
        g_worker_id = id;
        g_worker_base = worker_base;
        g_worker_listener = listener;

        worker_run();

        exit(0);
    }
}

static void __on_sigchild(int sig) {
    pid_t pid;
    while ( (pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        if (!g_workers) {
            continue;
        }

        int id = 0;
        for (id = 1; id <= g_worker_num; ++id) {
            if (pid != g_workers[id].pid) {
                continue;
            }

            // !!! g_workers[id].task失败处理
            g_workers[id].alive = 0;
            g_workers[id].busy = 0;

            if (__fork_worker(id)) {
                break;
            }
            break;
        }
    }
}

int init_workers(struct config *conf) {
    int worker_num = conf_get_int(conf, "cpunode:worker_num");
    if (worker_num <= 0) {
        loge("Couldn't get paramter: worker_num in config.\n");
        goto _E1;
    }

    int max_use = conf_get_int(conf, "cpunode:worker_max_use");
    if (max_use <= 0) {
        max_use = 400;
        logi("Couldn't get paramter: worker_max_use in config, using 400 now.\n");
    }

    size_t len = sizeof(worker_t)*(worker_num+1); // 0号是主进程
    g_workers = malloc(len);
    if (!g_workers) {
        loge("malloc g_workers failed.\n");
        goto _E1;
    }
    memset(g_workers, 0, len);
    g_worker_num = worker_num;
    g_worker_max_use = max_use;
    g_worker_id = 0; // 主进程worker_id = 0

    signal(SIGCHLD, __on_sigchild);

    int id;
    for (id = 1; id <= worker_num; ++id) {
        if (__fork_worker(id)) {
            goto _E1;
        }
    }
    return 0;

_E1:

    // !!! 忽略sigchld信号
    // !!! 退出所有子进程
    if (g_workers) {
        free(g_workers);
        g_workers = NULL;
    }
    return -1;
}

