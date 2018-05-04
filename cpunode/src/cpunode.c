#include "global.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static u_int8_t g_softquit = 0;

extern int master_proc(process_pool_t * pool);
extern int worker_proc(process_pool_t * pool);
static void __sig_softquit(int sig);
static int _fork_workers(struct config *conf);

int main(int argc, char *argv[]) {
    char *conf_path = "etc/cpunode.ini";
    int oc;
    int ret = -1;

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
        goto _E1;
    }

    if (g_init_log(g_conf)) {
        goto _E1;
    }

    if (g_load_vipkid_engine_cfg(g_conf)) {
        goto _E1;
    }

    g_base = event_base_new(); 
    if (!g_base) {
        loge("Couldn't create an event base.");
        goto _E1;
    }
    
    if (cpunode_httpd_init(g_conf, g_port)) {
        goto _E1;
    }

    //signal(SIGINT, __sig_softquit);
    //signal(SIGTERM, __sig_softquit);
    //
    printf("now see\n");
    sleep(10);

    ret = __fork_workers(g_conf);

    printf("never show this\n");

_E1:
    if (g_base) {
        event_base_free(g_base);
        g_base = NULL;
    }
    g_unload_vipkid_engine_cfg();

    if (g_conf) {
        conf_unload(g_conf);
        g_conf = NULL;
    }
    return ret;
}

static void __sig_softquit(int sig) {
    g_softquit = 1;
}

static int __fork_workers(struct config *conf) {
    int max_worker_num = conf_get_int(conf, "cpunode:max_worker_num");
    if (max_worker_num <= 0) {
        loge("Couldn't get paramter: max_worker_num in config.\n");
        return -1;
    }

    int max_worker_used = conf_get_int(conf, "cpunode:max_worker_used");
    if (max_worker_used <= 0) {
        max_worker_used = 400;
        logi("Couldn't get paramter: max_worker_used in config, using 400 now.\n");
    }

    return process_pool(&g_worker_pool, max_worker_num, max_worker_used, master_proc, worker_proc);
}
