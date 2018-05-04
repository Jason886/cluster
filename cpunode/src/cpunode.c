#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"
#include "wtk_vipkid_engine.h"

#include <event2/event.h>

#include "process_pool.h"

extern int master_proc(process_pool_t * pool);
extern int worker_proc(process_pool_t * pool);

wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg = NULL;
struct event_base *g_base = NULL;
process_pool_t g_worker_pool = {0};
struct config *g_conf = NULL;
int g_port = 9001;

static u_int8_t g_softquit = 0;
static void __sig_softquit(int sig) {
    g_softquit = 1;
}

static void init_logger_with_daemon(char *level, int is_daemon) {
    int log_level;

    switch (tolower(level[0])) {
            case 'n':
                log_level = LOG_NOTICE; break;
            case 'i':
                log_level = LOG_INFO; break;
            case 'w':
                log_level = LOG_WARNING; break;
            case 'e':
                log_level = LOG_ERR; break;
            case 'c':
                log_level = LOG_CRIT; break;
            default:
                log_level = LOG_DEBUG;
        }

    if (is_daemon) {
        daemon(0, 0);
        log_init(LOG_RSYSLOG, "local2");
    } else {
        log_init(LOG_STDERR, NULL);
    }
    
    fprintf(stderr, "loglevel: %d, is_daemon: %d.\n", log_level, is_daemon);

    log_set_level(log_level);
}

static int _init_log(struct config *conf) {
    char *log_level = conf_get_string(conf, "cpunode:loglevel");
    if (!log_level) {
        loge("Couldn't get paramter: loglevel in config.\n");
        return -1;
    }

    int daemon = conf_get_boolean(conf, "cpunode:daemon");
    init_logger_with_daemon(log_level, daemon);
    return 0;
}

static int _load_vipkid_engine_cfg(struct config *conf) {
    char *eval_res = conf_get_string(conf, "cpunode:eval_res");
    if (!eval_res) {
        eval_res = "res/eval_cfg";
        logi("Couldn't get paramter: eval_res in config, using res/eval_cfg now.\n");
    }

    int usebin = 0;

    if (!g_vipkid_engine_cfg) {
        logi("loading vipkid engine cfg ...\n");
        g_vipkid_engine_cfg = wtk_vipkid_engine_cfg_init(eval_res, usebin);
    }
    if (!g_vipkid_engine_cfg) {
        loge("loading vipkid engine cfg failed: %s %d.\n", eval_res, usebin);
        return -1;
    }
    logi("load vipkid engine cfg ok\n");
    return 0;
}

static void _unload_vipkid_engine_cfg() {
    if (g_vipkid_engine_cfg) {
        logi("unloading vipkid engine cfg ...\n");
        wtk_vipkid_engine_cfg_clean(g_vipkid_engine_cfg);
        g_vipkid_engine_cfg = NULL;
    }
}

static int _fork_workers(struct config *conf) {
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

    char *host = conf_get_string(conf, "cpunode:host");
    if (!host) { 
       host = "0.0.0.0";
       logi("Couldn't get paramter: host in config, using 0.0.0.0 now.\n");
    } 

    return process_pool(&g_worker_pool, max_worker_num, max_worker_used, master_proc, worker_proc);
}

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

    if (_init_log(g_conf)) {
        goto _E1;
    }

    if (_load_vipkid_engine_cfg(g_conf)) {
        goto _E1;
    }

    g_base = event_base_new(); 
    if (!g_base) {
        loge("Couldn't create an event base.");
        goto _E1;
    }

    //signal(SIGINT, __sig_softquit);
    //signal(SIGTERM, __sig_softquit);
    //
    printf("now see\n");
    sleep(10);

    ret = _fork_workers(g_conf);

_E1:
    if (g_base) {
        event_base_free(g_base);
        g_base = NULL;
    }
    _unload_vipkid_engine_cfg();

    if (g_conf) {
        conf_unload(g_conf);
        g_conf = NULL;
    }
    return ret;
}
