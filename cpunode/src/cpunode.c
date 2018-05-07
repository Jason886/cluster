#include "global.h"
#include "worker_pool.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

void worker_run() {
    worker_pool_t *pool = g_worker_pool;

    usleep(1000);
    printf("worker#%u setup\n", pool->idx);

    cpunode_httpd_free();
    event_reinit(g_base);
    event_base_free(g_base);
    g_base = NULL;
    g_base = event_base_new();
    if (!g_base) {
        exit(1);
    }
    event_base_dispatch(g_base);

    while(1) {
        usleep(10000);
        worker_t *worker = &pool->workers[pool->idx];
        worker->used++;
        if (worker->used >= pool->max_use) {
            printf("worker#%u max used exit\n", pool->idx);
            exit(1);
        }
    }

    printf("worker#%u exit\n", pool->idx);
    exit(0);
}

int main(int argc, char *argv[]) {
    char *conf_path = "etc/cpunode.ini";
    int oc;

    while ((oc = getopt(argc, argv, "c:p:")) != -1) {  
        switch (oc) {  
            case 'c':
                conf_path = optarg;  
                break;  
            case 'p':
                g_port = atoi(optarg);  
                break;
            default:
                printf("unknown options\n");  
                exit(EXIT_FAILURE);  
        }  
    }

    if (!(g_conf = conf_load(conf_path))) {
        loge("Couldn't get config from path: %s\n", conf_path);
        goto _E;
    }

    if (g_init_log(g_conf)) {
        goto _E;
    }

    g_base = event_base_new(); 
    if (!g_base) {
        loge("Couldn't create an event base.");
        goto _E;
    }

    //if (g_load_vipkid_engine_cfg(g_conf)) {
    //    goto _E;
    //}
    //
    
    if (cpunode_httpd_init(g_conf, g_port)) {
        goto _E;
    }
    
    if (init_worker_pool(g_conf)) {
        goto _E;
    }

    event_base_dispatch(g_base);
    return 0;

_E:
    cpunode_httpd_free();

    if (g_base) {
        event_base_free(g_base);
        g_base = NULL;
    }

    g_unload_vipkid_engine_cfg();

    if (g_conf) {
        conf_unload(g_conf);
        g_conf = NULL;
    }
    return -1;
}
