#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include <event.h>
#include <event2/http.h>
#include <event2/http_struct.h>

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

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

int main(int argc, char *argv[]) {
    char *conf_path = "etc/cpunode.ini";
    int port        = 9001;
    int oc;

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

    struct config *conf = conf_load(conf_path);
    if (!conf) {
        loge("Couldn't get config from path: %s\n", conf_path);
        return 1;
    }

    char *log_level = conf_get_string(conf, "cpunode:loglevel");
    if (!log_level) {
        loge("Couldn't get paramter: loglevel in config.\n");
        return 1;
    }

    int daemon = conf_get_boolean(conf, "cpunode:daemon");
    init_logger_with_daemon(log_level, daemon);

    char *host = conf_get_string(conf, "cpunode:host");
    if (!host) { 
       host = "0.0.0.0";
       logi("Couldn't get paramter: host in config, using 0.0.0.0 now.\n");
    } 

    int max_worker_num = conf_get_int(conf, "cpunode:max_worker_num");
    if (max_worker_num <= 0) {
        loge("Couldn't get paramter: max_worker_num in config.\n");
        return 1;
    }

    int max_worker_used = conf_get_int(conf, "cpunode:max_worker_used");
    if (max_worker_used <= 0) {
        max_worker_used = 400;
        logi("Couldn't get paramter: max_worker_used in config, using 400 now.\n");
    }

    char *eval_res = conf_get_string(conf, "cpunode:eval_res");
    if (!eval_res) {
        eval_res = "res/eval_cfg";
        logi("Couldn't get paramter: eval_res in config, using res/eval_cfg now.\n");
    }

    /*
    if (load_vipkid_engine_cfg(eval_res, 0) != 0) {
        goto fail_loadwtk;
    }
    */

    // 创建worker
    
    printf("here\n");

    return 0;
}
