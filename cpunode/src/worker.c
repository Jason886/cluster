#include "http_download.h"
#include "worker_pool.h"
#include "worker.h"
#include "err_inf.h"

#include "wtk_vipkid_engine.h"
#include "liblog.h"
#include "cJSON.h"

#include <string.h>

extern wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg;

unsigned int g_worker_id = 0;  // 工作进程编号, 从1开始, 0代表主进程
struct event_base *g_worker_base = NULL;
struct evconnlistener * g_worker_listener = NULL;

typedef enum {
    e_worker_state_idle = 0,    // 空闲
    e_worker_state_read_cmd_len = 1,
    e_worker_state_read_cmd_content = 2,
    e_worker_state_read_data_len = 3,
    e_worker_state_read_data_content = 4,
    e_worker_state_calc = 5,     // 计算
    e_worker_state_read_data = 6, // 从管道读
    e_worker_state_read_cmd = 7,
} e_worker_state;

typedef struct worker_fsm {
    e_worker_state state;
    struct bufferevent *bev; 

    char *cmd;
    size_t data_len;
    char *data;
    char *result;
} worker_fsm_t;

static worker_fsm_t __fsm = {0}; 

static void __reset_fsm_data() {
    if (__fsm.cmd) {
        free(__fsm.cmd);
        __fsm.cmd = NULL;
    } 
    if (__fsm.data) {
        free(__fsm.data);
        __fsm.data = NULL;
    }
    if (__fsm.result) {
        free(__fsm.result);
        __fsm.result = NULL;
    }
    __fsm.data_len = 0;
}

int __check_cmd_and_data(char *errmsg, size_t size) {
    int ret = -1;
    cJSON *jcmd = NULL;

    if (!__fsm.cmd) {
        snprintf(errmsg, size, "internal error, worker.c:__check_cmd_and_data() #1");
        goto _E;
    }
    jcmd = cJSON_Parse(__fsm.cmd);
    if (!jcmd) {
        snprintf(errmsg, size, "internal error, worker.c:__check_cmd_and_data() #2");
        goto _E;
    }

    if (__fsm.data == NULL || __fsm.data_len == 0) {
        cJSON *jfileurl = cJSON_GetObjectItem(jcmd, "fileurl");
        if (!jfileurl || jfileurl->type != cJSON_String || strlen(jfileurl->valuestring) == 0) {
            snprintf(errmsg, size, "internal error, worker.c:__check_cmd_and_data() #3");
            goto _E;
        }
    }

    ret = 0;

_E:
    if (jcmd) cJSON_Delete(jcmd);
    jcmd = NULL;
    return ret;
}

static char *__do_calc() {
    printf("__do_calc data = %s\n", __fsm.data);

    wtk_vipkid_engine_t *engine = wtk_vipkid_engine_new(g_vipkid_engine_cfg);
    if (!engine) {
        // error!!!
        return "error";
    }

    if (wtk_vipkid_engine_start(engine)) {
        // !!! error
        return "start error";
    }

    char buffer[4096];
    size_t pos = 44;
    while (pos + 4096 < __fsm.data_len) {
        wtk_vipkid_engine_feed_wav2(engine, __fsm.data + pos, 4096, 0);
        pos += 4096;
    }
    wtk_vipkid_engine_feed_wav2(engine, __fsm.data + pos, __fsm.data_len - pos, 1);
    
    char * engine_res = engine->res;
    if (!engine_res || strlen(engine_res) == 0) {
        // !!! error
        return "error";
    }

    char *result = strdup(engine_res);

    wtk_vipkid_engine_reset(engine);
    wtk_vipkid_engine_delete(engine);
    return result;
}

static void __result_callback_cb(int result, char *data, unsigned int size) {
    printf("result = %d\n", result);
    printf("data = %s\n", data);
    printf("size = %d\n", size);

    struct evbuffer *output = bufferevent_get_output(__fsm.bev);
    //evbuffer_add_printf(output, "what what what");;


    // 向管道写
    evbuffer_add(output, "\n", 1);
    evbuffer_add(output, WORKER_FRAME_MAGIC_HEAD, sizeof(WORKER_FRAME_MAGIC_HEAD));
    evbuffer_add(output, "\n", 1);
    evbuffer_add(output, "10\n", 3);
    evbuffer_add(output, "hellohello", 10);
    evbuffer_add(output, "0\n", 2);

    __fsm.state = e_worker_state_idle;
}

static void __result_callback(char *result) {
    printf("result_cb\n");

    // write

    if (http_post (
                g_worker_base, 
                "http://10.0.200.20:5001/callback",
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
        if (__fsm.data) {
            free(__fsm.data);
            __fsm.data = NULL;
        }
        __fsm.data = data;
        __fsm.data_len = size;

        char *result = __do_calc();
        __result_callback(result);
        free(result);
    } else {
        __result_callback("download failed");
    }
}

static void __calc() {
    struct evbuffer *output = bufferevent_get_output(__fsm.bev);
    char errmsg[512];
    printf("__check_cmd_and_data\n");

    if (__check_cmd_and_data(errmsg, sizeof(errmsg))) {
        printf("check_data error\n");
        evbuffer_add_printf(output, "{\"token\": \"%s\", \"error\":{\"errno\":%d,\"info\":\"%s\"}}", "!!!", 10011, errmsg);
        __fsm.state = e_worker_state_idle;
        return;
    }
    printf("after check_data\n");

    if (__fsm.data && __fsm.data_len > 0) {
        char *result = __do_calc();
        __result_callback(result);
        free(result);
        return;
    }
    printf("download start\n");

    if (http_download_start(
            g_worker_base, 
            "http://10.0.200.20:5001/test.wav",
            __download_cb, 
            NULL)) {
        __result_callback("download start failed");
        return;
    }
}

static void __worker_eventcb(struct bufferevent *bev, short events, void *user_data) {
    printf("__worker_eventcb\n");
}


static void __worker_writecb(struct bufferevent *bev, void *user_data) {
    printf("__worker_writecb\n");
    struct evbuffer *output = bufferevent_get_output(bev);
     if (evbuffer_get_length(output) == 0) {
         //printf("flushed answer\n");
         //bufferevent_free(bev);
     }
}

static void __worker_readcb(struct bufferevent *bev, void *user_data) {
    printf("__worker_readcb\n");
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);
    char *tmp;
    size_t tmp_size = 0;
    size_t magic_size = sizeof(WORKER_FRAME_MAGIC_HEAD);

    while (evbuffer_get_length(input) > 0) {
        switch(__fsm.state) {
            case e_worker_state_idle:
                tmp_size = 0;
                tmp = evbuffer_readln(input, &tmp_size, EVBUFFER_EOL_ANY);
                if (tmp) {
                    //printf("diuqi?\n");
                    if (tmp_size >= magic_size && 
                        memcmp(tmp+tmp_size-magic_size, WORKER_FRAME_MAGIC_HEAD, magic_size) == 0) {
                        __fsm.state = e_worker_state_read_cmd;
                        __reset_fsm_data();
                    } else {
                        //printf("diuqi yes\n");
                    }
                    free(tmp);
                    tmp = NULL;
                }
                break;
            case e_worker_state_read_cmd:
                //printf("read_cmd\n");
                tmp = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY);
                if (tmp) {
                    __fsm.cmd = tmp;
                    printf("cmd = %s\n", __fsm.cmd);
                    __fsm.state = e_worker_state_read_data_len;
                }
                break;
            case e_worker_state_read_data_len:
                //printf("read_data_len\n");
                __fsm.data_len = 0; 
                tmp = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY);
                if (tmp) {
                    printf("data_len = %s\n", tmp);
                    int len = atoi(tmp);
                    free(tmp);
                    tmp = NULL;
                    if (len < 0) len = 0;
                    __fsm.data_len = len;
                    if (__fsm.data_len == 0) {
                        __fsm.state = e_worker_state_calc;
                        printf("__calc1\n");
                        __calc();
                        break;
                    }
                    __fsm.state = e_worker_state_read_data;
                }
                break;
            case e_worker_state_read_data:
                printf("read data\n");

                if (evbuffer_get_length(input) >= __fsm.data_len) {
                    __fsm.data = malloc(__fsm.data_len);
                    memset(__fsm.data, 0, __fsm.data_len);
                    evbuffer_remove(input, __fsm.data, __fsm.data_len);
                    __fsm.state = e_worker_state_calc;
                    //printf("data = %s\n", __fsm.data);
                    printf("__calc2\n");
                    __calc();
                }
                break;
            case e_worker_state_calc:
                printf("calcing, drain\n");
                evbuffer_drain(input, evbuffer_get_length(input));
                break;
            default:
                break;
        }
    }
}

void worker_run() {
    usleep(1000);

    memset(&__fsm, 0, sizeof(__fsm));
    __fsm.state = e_worker_state_idle;

    logi("worker#%u setup ok, loop\n", g_worker_id);

    event_base_dispatch(g_worker_base);

    if (g_worker_listener) {
        evconnlistener_free(g_worker_listener);
        g_worker_listener = NULL;
    }
    if (g_worker_base) {
        event_base_free(g_worker_base);
        g_worker_base = NULL;
    }

    logi("worker#%u exit\n", g_worker_id);
    exit(0);
}

void worker_listener_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data) {
    if (__fsm.state != e_worker_state_idle) {
        loge("worker#%u got a connection when non-idle, then disconnect\n", g_worker_id);
        goto _E;
    }
    logd("worker#%u got a connection\n", g_worker_id);

    struct bufferevent *bev = bufferevent_socket_new(g_worker_base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        loge("worker#%u bufferevent_socket_new failed, then disconnect\n", g_worker_id);
        goto _E;
    }
    __fsm.bev = bev;
    bufferevent_setcb(bev, __worker_readcb, __worker_writecb, __worker_eventcb, NULL);
    bufferevent_enable(bev, EV_WRITE);
    bufferevent_enable(bev, EV_READ);

    return;

_E:
    if (evutil_closesocket(fd)) {
        loge("worker#%u evutil_closesocket failed, then break\n", g_worker_id);
        sleep(3);
        event_base_loopbreak(g_worker_base);
        return;
    }
    return;
}

void worker_listener_error_cb(struct evconnlistener *listener, void *ctx) {
    loge("worker#%u got evconnlistener error, then exit(1)\n", g_worker_id);
    sleep(3);
    exit(1);
}
