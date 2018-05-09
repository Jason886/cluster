#include "worker_pool.h"
#include "wtk_vipkid_engine.h"
#include "http_download.h"
#include "err_inf.h"
#include "cJSON.h"

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#include <event.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

extern struct event_base *g_base;
extern wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg;

typedef enum {
    e_worker_state_idle = 0,    // 空闲
    e_worker_state_read_cmd = 1,
    e_worker_state_read_data_len = 2,
    e_worker_state_read_data = 3, // 从管道读
    e_worker_state_down_data = 4, // 从网络下载数据
    e_worker_state_calc = 5     // 计算
} e_worker_state;

typedef struct worker_private {
    worker_t *worker;
    e_worker_state state;
    struct event_base *base;
    struct bufferevent *bev; 

    char *cmd;
    size_t data_len;
    char *data;
} worker_private_t;

static worker_private_t _worker_pri = {0}; 


static void __reset_data() {
    if (_worker_pri.cmd) {
        free(_worker_pri.cmd);
        _worker_pri.cmd = NULL;
    } 
    if (_worker_pri.data) {
        free(_worker_pri.data);
        _worker_pri.data = NULL;
    }
    _worker_pri.data_len = 0;
}

int __check_data() {
    int ret = -1;
    cJSON *jcmd = NULL;
    cJSON *jfileurl = NULL;

    if (!_worker_pri.cmd) {
        goto _E;
    }
    jcmd = cJSON_Parse(_worker_pri.cmd);
    if (!jcmd) {
        goto _E;
    }

    jfileurl = cJSON_GetObjectItem(jcmd, "fileurl");
    if (!jfileurl || jfileurl->type != cJSON_String || jfileurl->valuestring[0] == 0) {
        if (_worker_pri.data == NULL || _worker_pri.data_len == 0) {
            goto _E;
        }
    }

    ret = 0;

_E:
    if (jcmd) cJSON_Delete(jcmd);
    jcmd = NULL;
    return ret;
}

static void __do_calc() {
    printf("__do_calc data = %s\n", _worker_pri.data);
}

static void __result_callback_cb(int result, char *data, unsigned int size) {
    printf("result = %d\n", result);
    printf("data = %s\n", data);
    printf("size = %d\n", size);
}

static void __result_callback() {
    printf("result_cb\n");

    if (http_post (
                _worker_pri.base, 
                "http://192.168.2.9:5001/callback",
                "this is result\n", 
                strlen("this is result"), 
                __result_callback_cb,
                NULL
            )
        ) {
        
        printf("post failed\n");
    }
}

static void __download_cb(int result, char *data, unsigned int size) {
    printf("download cb\n");
    if (result == 0 && data && size > 0) {
        if (_worker_pri.data) {
            free(_worker_pri.data);
            _worker_pri.data = NULL;
        }
        _worker_pri.data = data;
        _worker_pri.data_len = size;

        __do_calc();
        __result_callback();
    } else {
        __result_callback();
    }
}

static void __calc() {
    struct evbuffer *output = bufferevent_get_output(_worker_pri.bev);
    printf("calc\n");

    if (__check_data()) {
        printf("check_data error\n");
        const char *errmsg = cpunode_errmsg(12345); // !!! errorno
        evbuffer_add_printf(output, "{\"error\":{\"errno\":%d,\"info\":\"%s\"}}", errno, errmsg);
        _worker_pri.state = e_worker_state_idle;
        return;
    }

    if (_worker_pri.data && _worker_pri.data_len > 0) {
        __do_calc();
        __result_callback();
        return;
    }


    const char * url = "http://www.baidu.com/what?haha=kkk&uiuuu=oooo#heihei";
    struct evhttp_uri * ev_uri = evhttp_uri_parse(url);
    printf("ev_uri = %p\n", ev_uri);
    printf("!!!! host = %s\n", evhttp_uri_get_host(ev_uri));
    printf("!!!! path = %s\n", evhttp_uri_get_path(ev_uri));
    printf("!!!! prot = %d\n", evhttp_uri_get_port(ev_uri));
    printf("!!!! q = %s\n", evhttp_uri_get_query(ev_uri));
    printf("!!! scheme = %s\n", evhttp_uri_get_scheme(ev_uri));
    printf("!!! frag =%s\n", evhttp_uri_get_fragment(ev_uri));
    printf("!!! user = %s\n", evhttp_uri_get_userinfo (ev_uri));

    evhttp_uri_free(ev_uri);

    if (http_download_start(
            _worker_pri.base,
            "http://192.168.2.9:5001/test.txt",
            __download_cb, 
            NULL)) {
        __result_callback();
        return;
    }
}



static void __worker_readcb(struct bufferevent *bev, void *user_data) {
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);
    char *tmp;
    size_t tmp_size = 0;
    size_t magic_size = sizeof(WORKER_FRAME_MAGIC_HEAD);

    while (evbuffer_get_length(input) > 0) {
        switch(_worker_pri.state) {
            case e_worker_state_idle:
                tmp_size = 0;
                tmp = evbuffer_readln(input, &tmp_size, EVBUFFER_EOL_ANY);
                if (tmp) {
                    printf("diuqi?\n");
                    if (tmp_size >= magic_size && 
                        memcmp(tmp+tmp_size-magic_size, WORKER_FRAME_MAGIC_HEAD, magic_size) == 0) {
                        _worker_pri.state = e_worker_state_read_cmd;
                        __reset_data();
                    } else {
                        printf("diuqi yes\n");
                    }
                    free(tmp);
                    tmp = NULL;
                }
                break;
            case e_worker_state_read_cmd:
                printf("read_cmd\n");
                tmp = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY);
                if (tmp) {
                    _worker_pri.cmd = tmp;
                    printf("cmd = %s\n", _worker_pri.cmd);
                    _worker_pri.state = e_worker_state_read_data_len;
                }
                break;
            case e_worker_state_read_data_len:
                printf("read_data_len\n");
                _worker_pri.data_len = 0; 
                tmp = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY);
                if (tmp) {
                    printf("data_len = %s\n", tmp);
                    int len = atoi(tmp);
                    free(tmp);
                    tmp = NULL;
                    if (len < 0) len = 0;
                    _worker_pri.data_len = len;
                    _worker_pri.state = e_worker_state_read_data;
                }
                break;
            case e_worker_state_read_data:
                if (_worker_pri.data_len == 0) {
                    _worker_pri.state = e_worker_state_calc;
                    printf("entry1\n");
                    __calc();
                    //__download(_worker_pri.base, "10.0.200.20", 5005);
                    break;
                }

                if (evbuffer_get_length(input) >= _worker_pri.data_len) {
                    _worker_pri.data = malloc(_worker_pri.data_len);
                    memset(_worker_pri.data, 0, _worker_pri.data_len);
                    evbuffer_remove(input, _worker_pri.data, _worker_pri.data_len);
                    _worker_pri.state = e_worker_state_calc;
                    printf("data = %s\n", _worker_pri.data);
                    printf("entry2\n");
                    __calc();
                    // 计算
                }
                break;
            case e_worker_state_calc:
                printf("drain\n");
                //if (evbuffer_get_length(input)) {
                //}
                evbuffer_drain(input, evbuffer_get_length(input));
                break;
            default:
                break;
        }
    }
}

void worker_run() {
    worker_pool_t *pool = g_worker_pool;

    usleep(1000);
    logi("worker#%u setup\n", pool->idx);

    event_reinit(g_base);
    cpunode_httpd_free();
    event_base_free(g_base);
    g_base = NULL;

    struct event_base *base = event_base_new();
    if (!base) {
        loge("worker#%u event_base_new failed\n", pool->idx);
        sleep(2);
        exit(1);
    }

    worker_t *worker = &(pool->workers[pool->idx]);

    struct bufferevent *bev = bufferevent_socket_new(base, worker->pipefd[0], 0);
    if (!bev) {
        loge("worker#%u bufferevent_socket_new failed.\n");
        sleep(2);
        exit(1);
    }

    bufferevent_setcb(bev, __worker_readcb, NULL, NULL, NULL);
    bufferevent_enable(bev, EV_READ);
    bufferevent_enable(bev, EV_WRITE);

    memset(&_worker_pri, 0, sizeof(_worker_pri));
    _worker_pri.worker = &pool->workers[pool->idx];
    _worker_pri.state = e_worker_state_idle;
    _worker_pri.base = base;
    _worker_pri.bev = bev;

    logi("worker#%u setup ok, loop\n", pool->idx);

    event_base_dispatch(base);

    logi("worker#%u exit\n", pool->idx);
    exit(0);
}
