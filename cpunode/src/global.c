#include "global.h"

struct config *g_conf = NULL;
wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg = NULL;
struct event_base *g_base = NULL;
cpunode_httpd_t g_httpd = {0};
int g_port = 9001;

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

int g_init_log(struct config *conf) {
    char *log_level = conf_get_string(conf, "cpunode:loglevel");
    if (!log_level) {
        loge("Couldn't get paramter: loglevel in config.\n");
        return -1;
    }

    int daemon = conf_get_boolean(conf, "cpunode:daemon");
    init_logger_with_daemon(log_level, daemon);
    return 0;
}

int g_load_vipkid_engine_cfg(struct config *conf) {
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

void g_unload_vipkid_engine_cfg() {
    if (g_vipkid_engine_cfg) {
        logi("unloading vipkid engine cfg ...\n");
        wtk_vipkid_engine_cfg_clean(g_vipkid_engine_cfg);
        g_vipkid_engine_cfg = NULL;
    }
}
