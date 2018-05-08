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

struct config *g_conf = NULL;
struct event_base *g_base = NULL;
wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg = NULL;

static int __init_log(struct config *conf) {
    char *level = conf_get_string(conf, "cpunode:loglevel");
    if (!level) {
        loge("Couldn't get paramter: loglevel in config.\n");
        return -1;
    }

    int is_daemon = conf_get_boolean(conf, "cpunode:daemon");

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
    
    logi("loglevel: %d, is_daemon: %d.\n", log_level, is_daemon);

    log_set_level(log_level);

    return 0;
}

static int __load_vipkid_engine_cfg(struct config *conf) {
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

static void __unload_vipkid_engine_cfg() {
    if (g_vipkid_engine_cfg) {
        logi("unloading vipkid engine cfg ...\n");
        wtk_vipkid_engine_cfg_clean(g_vipkid_engine_cfg);
        g_vipkid_engine_cfg = NULL;
    }
}

int main(int argc, char *argv[]) {
    char *conf_path = "etc/cpunode.ini";
    int oc;
    int port = 9001; // 默认9001

    while ((oc = getopt(argc, argv, "c:p:")) != -1) {  
        switch (oc) {  
            case 'c':
                conf_path = optarg;  
                break;  
            case 'p':
                port = atoi(optarg);  
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

    if (__init_log(g_conf)) {
        goto _E;
    }

    g_base = event_base_new(); 
    if (!g_base) {
        loge("Couldn't create an event base.");
        goto _E;
    }

    //if (__load_vipkid_engine_cfg(g_conf)) {
    //    goto _E;
    //}
    //
    
    if (cpunode_httpd_init(g_conf, port)) {
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

    __unload_vipkid_engine_cfg();

    if (g_conf) {
        conf_unload(g_conf);
        g_conf = NULL;
    }
    return -1;
}
