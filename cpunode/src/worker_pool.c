#include "worker_pool.h"

#include "liblog.h"

#include <event.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/listener.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

extern void worker_run();
extern void worker_listener_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data);
extern void worker_accept_error_cb(struct evconnlistener *listener, void *ctx);
extern struct event_base *g_base;
extern int g_base_worker_port;

worker_pool_t *g_worker_pool = NULL;
struct event_base *g_worker_base = NULL;
struct evconnlistener * g_worker_listener = NULL;


static void __on_sigchild(int sig) {
    worker_pool_t *pool = g_worker_pool;
    int ix = 0;
    int flags = 0;
    pid_t pid;

    while ( (pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        if (pool) {
            for (ix = 1; ix <= pool->worker_num; ++ix) {
                if (pid != pool->workers[ix].pid) {
                    continue;
                }

                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_addr.s_addr = htonl(0);
                printf("worker#%u listen %d\n", ix, g_base_worker_port+ix);
                sin.sin_port = htons(g_base_worker_port+ix);

                pool->workers[ix].alive = 0;
                pool->workers[ix].busy = 0;
                pool->workers[ix].used = 0;
                //if (pool->workers[ix].bev) {
                //    bufferevent_free(pool->workers[ix].bev);
                //    pool->workers[ix].bev = NULL;
                //}
                //close(pool->workers[ix].pipefd[1]);

                // !!! 将task列表中该项的绑定删除 // 并取消bev的监听
                
                struct event_base * worker_base = event_base_new();
                if (!worker_base) {
                    loge("create event_base for worker#%u failed\n", ix);
                    continue;
                }

                struct evconnlistener *listener = evconnlistener_new_bind(worker_base, worker_listener_cb, NULL, 
                        LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1, 
                        (struct sockaddr*)&sin, sizeof(sin));
                if (!listener) {
                    loge("listener at %d for worker#%u failed\n", g_base_worker_port+ix, ix);
                    // !!! close base
                    continue;
                }
                evconnlistener_set_error_cb(listener, worker_accept_error_cb);

                //if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pool->workers[ix].pipefd)) {
                //    loge("evutil_socketpair failed\n");
                //    continue;
                //}

                //flags = fcntl(pool->workers[ix].pipefd[0], F_GETFL, 0);
                //fcntl(pool->workers[ix].pipefd[0], F_SETFL, flags|O_NONBLOCK);
                //flags = fcntl(pool->workers[ix].pipefd[1], F_GETFL, 0);
                //fcntl(pool->workers[ix].pipefd[1], F_SETFL, O_NONBLOCK);

                pool->workers[ix].pid = fork();
                if (pool->workers[ix].pid < 0) {
                    loge("fork worker#%u failed\n", ix);
                    evconnlistener_free(listener);
                    event_base_free(worker_base);

                    //close(pool->workers[ix].pipefd[0]);
                    //close(pool->workers[ix].pipefd[1]);
                    continue;
                }

                if (pool->workers[ix].pid > 0) {
                    // father
                    logi("fork worker#%u ok, pid = %d\n", ix, pool->workers[ix].pid);
                    evconnlistener_free(listener);
                    event_base_free(worker_base);
                    //close(pool->workers[ix].pipefd[0]);
                    //pool->workers[ix].bev = bufferevent_socket_new(g_base, pool->workers[ix].pipefd[1], 0);
                    /* !!! 如果创建失败
                    if() {
                    }
                    */
                    pool->workers[ix].alive = 1;
                    continue; 
                } else {
                    // child
                    //int iii = 0;
                    //for (iii = 1; iii <= pool->worker_num; ++iii) {
                    //    if (iii != ix) {
                    //        close(pool->workers[iii].pipefd[1]);
                    //    }
                    //}
                    event_reinit(g_base);
                    cpunode_httpd_free();
                    event_base_free(g_base);
                    g_base = NULL;

                    g_worker_base = worker_base;
                    g_worker_listener = listener;
                    pool->idx = ix;

                    worker_run();
                    exit(0);
                }
            }
        }
    }
}

int init_worker_pool(struct config *conf) {
    int max_worker_num = 0, max_worker_used = 0;
    int ix = 0, pair_count =0, fork_count = 0, flags = 0;
    size_t len = 0;
    worker_pool_t *pool = NULL;

    max_worker_num = conf_get_int(conf, "cpunode:max_worker_num");
    if (max_worker_num <= 0) {
        loge("Couldn't get paramter: max_worker_num in config.\n");
        goto _E1;
    }

    max_worker_used = conf_get_int(conf, "cpunode:max_worker_used");
    if (max_worker_used <= 0) {
        max_worker_used = 400;
        logi("Couldn't get paramter: max_worker_used in config, using 400 now.\n");
    }

    len = sizeof(worker_pool_t) + sizeof(worker_t)*(max_worker_num+1);
    pool = g_worker_pool = malloc(len);
    if (!g_worker_pool) {
        loge("malloc worker pool struct failed.\n");
        goto _E1;
    }
    memset(pool, 0, len);
    pool->idx = 0;
    pool->worker_num = max_worker_num;
    pool->max_use = max_worker_used;

    signal(SIGCHLD, __on_sigchild);

    for (ix = 1; ix <= max_worker_num; ++ix) {
        pool->workers[ix].used = 0;
        pool->workers[ix].alive = 0;
        pool->workers[ix].busy = 0;

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0);
        printf("worker#%u listen %d\n", ix, g_base_worker_port+ix);
        sin.sin_port = htons(g_base_worker_port+ix);

        struct event_base * worker_base = event_base_new();
        if (!worker_base) {
            loge("create event_base for worker#%u failed\n", ix);
            goto _E2;
        }

        struct evconnlistener *listener = evconnlistener_new_bind(worker_base, worker_listener_cb, NULL, 
                LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1, 
                (struct sockaddr*)&sin, sizeof(sin));
        if (!listener) {
            loge("listener at %d for worker#%u failed\n", g_base_worker_port+ix, ix);
            goto _E2;
        }

        evconnlistener_set_error_cb(listener, worker_accept_error_cb);


        //if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0,  pool->workers[ix].pipefd)) {
        //    loge("evutil_socketpair failed\n");
        //    goto _E2;
        //}
        //pair_count = ix;

        //flags = fcntl(pool->workers[ix].pipefd[0], F_GETFL, 0);
        //fcntl(pool->workers[ix].pipefd[0], F_SETFL, flags|O_NONBLOCK);
        //flags = fcntl(pool->workers[ix].pipefd[1], F_GETFL, 0);
        //fcntl(pool->workers[ix].pipefd[1], F_SETFL, O_NONBLOCK);

        pool->workers[ix].pid = fork();
        if (pool->workers[ix].pid < 0) {
            logi("fork worker#%u failed\n", ix);
            goto _E2;
        }
        fork_count = ix;

        if (pool->workers[ix].pid > 0) {
            // father
            logi("fork worker#%u ok, pid = %d\n", ix, pool->workers[ix].pid);
            evconnlistener_free(listener);
            event_base_free(worker_base);
            
            //close(pool->workers[ix].pipefd[0]);
            //pool->workers[ix].bev = bufferevent_socket_new(g_base, pool->workers[ix].pipefd[1], 0);
            /* !!! 如果创建失败
            if() {
            }
            */
            pool->workers[ix].alive = 1;
            continue; 
        } else {
            // child
            //int iii = 0;
            //for (iii = 1; iii <= pair_count; ++iii) {
            //    close(pool->workers[iii].pipefd[1]);
            //}
            
            event_reinit(g_base);
            cpunode_httpd_free();
            event_base_free(g_base);
            g_base = NULL;

            event_reinit(worker_base);
            g_worker_base = worker_base;
            g_worker_listener = listener;
            pool->idx = ix;

            worker_run();
            exit(0);
        }
    }

    return 0;

_E2:

    /*
    for (ix = 1; ix <= pair_count; ++ix) {
        close(pool->workers[ix].pipefd[1]);
        if (ix > fork_count) {
            close(pool->workers[ix].pipefd[0]);
        }
    }
    */
    //free(pool->workers);

_E1:
    if (g_worker_pool) {
        free(g_worker_pool);
        g_worker_pool = NULL;
    }
    return -1;
}

