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
    e_worker_state_read_cmd = 2,
    e_worker_state_read_data_len = 3,
    e_worker_state_read_data = 4,
    e_worker_state_calc = 5,     // 计算
    e_worker_state_write_result = 6,
} e_worker_state;

typedef struct worker_fsm {
    e_worker_state state;
    struct bufferevent *bev; 

    u_int32_t cmd_len;
    char *cmd;
    struct cJSON *jcmd;
    u_int32_t data_len;
    char *data;
    u_int32_t result_len;
    char *result;
} worker_fsm_t;

static worker_fsm_t _fsm = {0}; 

static void __reset_fsm_data() {
    if (_fsm.cmd) {
        free(_fsm.cmd);
        _fsm.cmd = NULL;
    } 
    if (_fsm.jcmd) {
        cJSON_Delete(_fsm.jcmd);
        _fsm.jcmd = NULL;
    }
    if (_fsm.data) {
        free(_fsm.data);
        _fsm.data = NULL;
    }
    if (_fsm.result) {
        free(_fsm.result);
        _fsm.result = NULL;
    }
    _fsm.cmd_len = 0;
    _fsm.data_len = 0;
    _fsm.result_len = 0;
}

static int __make_result(struct cJSON *j_res, char **errmsg) {
    if (!j_res) return -1;

    struct cJSON *j_time_rate = cJSON_GetObjectItem(j_res, "time_rate");
    //if (!j_time_rate || j_time_rate->type != cJSON_Number) {
    //    *errmsg = "internal error, engine result no time_rate";
    //    return -1;
    //}
    struct cJSON *j_speech_time = cJSON_GetObjectItem(j_res, "speech_time");
    if (!j_speech_time || j_speech_time->type != cJSON_Number) {
        *errmsg = "internal error, engine result no speech_time";
        return -1;
    }
    struct cJSON *j_speech_time_per = cJSON_GetObjectItem(j_res, "speech_time_per");
    if (!j_speech_time_per || j_speech_time_per->type != cJSON_Number) {
        *errmsg = "internal error, engine result no speech_time_per";
        return -1;
    }
    struct cJSON *j_eng_time = cJSON_GetObjectItem(j_res, "eng_time");
    if (!j_eng_time || j_eng_time->type != cJSON_Number) {
        *errmsg = "internal error, engine result no eng_time";
        return -1;
    }
    struct cJSON *j_eng_time_per = cJSON_GetObjectItem(j_res, "eng_time_per");
    if (!j_eng_time_per || j_eng_time_per->type != cJSON_Number) {
        *errmsg = "internal error, engine result no eng_time_per";
        return -1;
    }
    struct cJSON *j_wrd_nums = cJSON_GetObjectItem(j_res, "wrd_nums");
    if (!j_wrd_nums || j_wrd_nums->type != cJSON_Number) {
        *errmsg = "internal error, engine result no wrd_nums";
        return -1;
    }
    struct cJSON *j_wrd_nums_div_time = cJSON_GetObjectItem(j_res, "wrd_nums_div_time");
    if (!j_wrd_nums_div_time || j_wrd_nums_div_time->type != cJSON_Number) {
        *errmsg = "internal error, engine result no wrd_nums_div_time";
        return -1;
    }

    const char *fmt = "{"
            "\"token\":%s%s%s,"
            "\"data\": {"
                "\"word\": {\"count\": %d, \"rate\": %0.4f},"
                "\"voice\": {\"duration\": %0.4f, \"percent\": %0.4f},"
                "\"english\": {\"duration\": %0.4f, \"percent\": %0.4f},"
                "\"time_rate\": %s"
            "},"
            "\"request\":%s"
        "}";

    struct cJSON *j_token = cJSON_GetObjectItem(_fsm.jcmd, "token");
    char *token = NULL;
    if (j_token && j_token->type == cJSON_String) {
        token = j_token->valuestring; 
    }
    
    size_t len = strlen(fmt) + (token?strlen(token):4) +  (_fsm.jcmd?strlen(_fsm.cmd):4) + 256;
    char *result = malloc(len);
    if (!result) {
        *errmsg = "internal error, __make_result: malloc result failed";
        return -1;
    }

    char time_rate[30];
    strcpy(time_rate, "null");
    if (j_time_rate && j_time_rate->type == cJSON_Number) {
        snprintf(time_rate, sizeof(time_rate), "%0.4f", j_time_rate->valuedouble);
    }

    snprintf(result, len, fmt,
            token?"\"":"",
            token?token:"null",
            token?"\"":"",

            j_wrd_nums->valueint,
            j_wrd_nums_div_time->valuedouble,
            j_eng_time->valuedouble,
            j_eng_time_per->valuedouble,
            j_speech_time->valuedouble,
            j_speech_time_per->valuedouble,
            time_rate,

            _fsm.jcmd?_fsm.cmd:"null"
        );
    
    if (_fsm.result) free(_fsm.result);
    _fsm.result = result;
    _fsm.result_len = strlen(result);
    return 0;
}

static void __write_result() {
    u_int32_t len = _fsm.result_len;
    struct evbuffer *output = bufferevent_get_output(_fsm.bev);

    int write_ret = evbuffer_add(output, &len, sizeof(len));
    if (write_ret == 0) { // suc
        if (len > 0) write_ret = evbuffer_add(output, _fsm.result, len);
    }
    __reset_fsm_data();
    if (write_ret != 0) { // fail
        if (_fsm.bev) bufferevent_free(_fsm.bev);
        _fsm.bev = NULL;
        _fsm.state = e_worker_state_idle;
    }

    return;
}

static void __make_error_result(int errno, const char *info) {
    if (!info) info = cpunode_errmsg(errno);
    if (!info) info = "";

    const char *errfmt = "{"
            "\"token\": %s%s%s,"
            "\"error\": {\"errno\":%d, \"info\":\"%s\"},"
            "\"request\": %s"
        "}"; 

    char *token = NULL;
    if (_fsm.jcmd) {
        struct cJSON *j_token = cJSON_GetObjectItem(_fsm.jcmd, "token");
        if (j_token && j_token->type == cJSON_String) {
            token = j_token->valuestring; 
        }
    }

    size_t len = strlen(errfmt) + (token?strlen(token):4) + (_fsm.jcmd?strlen(_fsm.cmd):4) + strlen(info) + 64;
    char *result = malloc(len);
    if (result) {
        memset(result, 0, len);
        snprintf(result, len, errfmt,
                token?"\"":"",
                token?token:"null",
                token?"\"":"",
                errno, info,
                _fsm.jcmd?_fsm.cmd:"null"
            );
    }
    
    if (_fsm.result) free(_fsm.result);
    _fsm.result = result;
    _fsm.result_len = result ? strlen(result) : 0;
}

static void __write_error(int errno, char *info) {
    __make_error_result(errno, info);
    __write_result();
}

static int __check_fea_data(char *data, size_t size) {
    uint32_t vad_len, rec_len;
    if(size < 4) {
        return -1;
    }
    memcpy(&vad_len, data, 4);
    
    if(size < vad_len+4 + 4) {
        return -1;
    }
    memcpy(&rec_len, data+vad_len+4, 4);
    if(size < vad_len + rec_len + 8) {
        return -1;
    }

    if(vad_len/24 != rec_len/40) {
        return -1;
    }

    return 0;
}

static void __do_calc() {
    int is_fea = 0;

    struct cJSON *j_isfea = cJSON_GetObjectItem(_fsm.jcmd, "isfea");
    if (j_isfea && j_isfea->type == cJSON_Number && j_isfea->valueint == 1) {
        is_fea = 1;
    }

    if (is_fea) {
        if (__check_fea_data(_fsm.data, _fsm.data_len)) {
            loge("worker#%u invalid fea data. %s\n", g_worker_id, _fsm.cmd);
            _fsm.state = e_worker_state_write_result;
            __write_error(10012, NULL);
            return;
        }
    }

    wtk_vipkid_engine_t *engine = wtk_vipkid_engine_new(g_vipkid_engine_cfg);
    if (!engine) {
        loge("worker#%u wtk_vipkid_engine_new failed. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, engine new failed");
        return;
    }

    if (wtk_vipkid_engine_start(engine)) {
        loge("worker#%u wtk_vipkid_engine_start failed. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, engine start failed");
        wtk_vipkid_engine_delete(engine);
        return;
    }

    int ret = 0;

    if (is_fea) {
        ret = wtk_vipkid_engine_feed_feautre2(engine, _fsm.data, _fsm.data_len);
    }
    else {
        char buffer[4096];
        size_t pos = 44;
        while (pos + 4096 < _fsm.data_len) {
            ret = wtk_vipkid_engine_feed_wav2(engine, _fsm.data + pos, 4096, 0);
            pos += 4096;
            if (ret) { break; }
        }
        if (!ret) {
            ret = wtk_vipkid_engine_feed_wav2(engine, _fsm.data + pos, _fsm.data_len - pos, 1);
        }
    }
    if (ret) {
        loge("worker#%u wtk_vipkid_engine_feed_* failed. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, engine feed failed");
        wtk_vipkid_engine_delete(engine);
        return;
    }
    
    char *res = engine->res;
    /*
    char *res = "{\"time_rate\":0.0562,\"speech_time\":0.0137,\"speech_time_per\":0.3309,\"eng_time\":0.0060,\"eng_time_per\":0.1462,\"wrd_nums\":21,\"wrd_nums_div_time\":507.7915}";
    */

    if (!res || strlen(res) == 0) {
        loge("worker#%u wtk_vipkid_engine result empty. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, engine result empty");
        wtk_vipkid_engine_delete(engine);
        return;
    }

    struct cJSON *j_res = cJSON_Parse(res);
    wtk_vipkid_engine_delete(engine); engine = NULL;
    if (!j_res || j_res->type != cJSON_Object) {
        loge("worker#%u wtk_vipkid_engine result invalid json. %s ||| request: %s\n", g_worker_id, res, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, engine result invalid json");
        if (j_res) cJSON_Delete(j_res);
        return;
    }

    char *errmsg = NULL;
    if (__make_result(j_res, &errmsg)) {
        loge("worker#%u __make_result failed. %s. %s ||| %s\n", g_worker_id, errmsg, res, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, errmsg);
        cJSON_Delete(j_res);
        return;
    }

    cJSON_Delete(j_res);

    logd("worker#%u success. %s\n", g_worker_id, _fsm.result);
    _fsm.state = e_worker_state_write_result;
    __write_result();
    return;
}

static void __download_cb(int result, char *data, unsigned int size) {
    logd("__download_cb result = %d, data = %p, size = %u\n", result, data, size);
    if (result != 0) {
        loge("worker#%u download failed from fileurl. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, download failed from fileurl");
        if (data) free(data);
        return;
    }
    if (!data || size == 0) {
        loge("worker#%u download an empty file from fileurl. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10013, "internal error, download an empty file from fileurl");
        if (data) free(data);
        return;
    }

    if (_fsm.data) free(_fsm.data);
    _fsm.data = data;
    _fsm.data_len = size;
    __do_calc();
}

static void __calc() {
    if (_fsm.data) {
        __do_calc();
        return;
    }

    struct cJSON *jfileurl = cJSON_GetObjectItem(_fsm.jcmd, "fileurl");
    if (http_download(g_worker_base, jfileurl->valuestring, __download_cb, NULL)) {
        loge("worker#%u download failed from fileurl. %s\n", g_worker_id, _fsm.cmd);
        _fsm.state = e_worker_state_write_result;
        __write_error(10011, "internal error, download failed from fileurl");
        return;
    }
}

static int __read_buffer(struct evbuffer *buffer, char *data, size_t size) {
    size_t left_n = size;
    while (left_n > 0) {
        int read_n = evbuffer_remove(buffer, data + size - left_n, left_n);
        if (read_n < 0) {
            loge("worker#%u evbuffer_remove failed\n", g_worker_id);
            return -1;
        }
        left_n -= read_n;
    }
    return 0;
}

static void __worker_eventcb(struct bufferevent *bev, short events, void *user_data) {
    if (bev == _fsm.bev) {
        if (events & BEV_EVENT_EOF) {
            logd("worker#%u bev eof\n", g_worker_id);
            __reset_fsm_data();
            if (_fsm.bev) bufferevent_free(_fsm.bev);
            _fsm.bev = NULL;
            _fsm.state = e_worker_state_idle;
        } else if (events & BEV_EVENT_ERROR) {
            loge("worker#%u bev error\n", g_worker_id);
            __reset_fsm_data();
            if (_fsm.bev) bufferevent_free(_fsm.bev);
            _fsm.bev = NULL;
            _fsm.state = e_worker_state_idle;
        }
    }
}

static void __worker_writecb(struct bufferevent *bev, void *user_data) {
    if (_fsm.state == e_worker_state_write_result) {
        struct evbuffer *output = bufferevent_get_output(bev);
        if (evbuffer_get_length(output) == 0) {
            logi("worker#%u write done, now close connection\n", g_worker_id);
            if (_fsm.bev) bufferevent_free(_fsm.bev);
            _fsm.bev = NULL;
            _fsm.state = e_worker_state_idle;
        }
    }
}

static void __worker_readcb(struct bufferevent *bev, void *user_data) {
    struct evbuffer *input = bufferevent_get_input(bev);
    u_int32_t cmd_len = 0;
    u_int32_t data_len = 0;

    while (evbuffer_get_length(input) > 0) {
        switch(_fsm.state) {

            case e_worker_state_idle:
                return;

            case e_worker_state_read_cmd_len:
                if (evbuffer_get_length(input) < sizeof(cmd_len)) {
                    return;
                }
                if (__read_buffer(input, (char *)&cmd_len, sizeof(cmd_len))) {
                    loge("worker#%u read cmd_len failed\n", g_worker_id);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, read cmd_len failed");
                    return;
                }
                if (cmd_len == 0) {
                    loge("worker#%u read cmd_len: 0\n", g_worker_id);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, read cmd_len: 0");
                    return;
                }
                _fsm.cmd_len = cmd_len;
                _fsm.cmd = malloc(cmd_len+1);
                if (!_fsm.cmd) {
                    loge("worker#%u malloc cmd failed\n", g_worker_id);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, malloc cmd failed");
                    return;
                }
                memset(_fsm.cmd, 0, cmd_len+1);
                _fsm.state = e_worker_state_read_cmd;
                break;

            case e_worker_state_read_cmd:
                if (evbuffer_get_length(input) < _fsm.cmd_len) {
                    return;
                }
                if (__read_buffer(input, _fsm.cmd, _fsm.cmd_len)) {
                    loge("worker#%u read cmd failed\n", g_worker_id);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, read cmd failed");
                    return;
                }
                struct cJSON *jcmd = cJSON_Parse(_fsm.cmd);
                if (!jcmd) {
                    loge("worker#%u cmd invalid json. %s\n", g_worker_id, _fsm.cmd);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, cmd invalid json");
                    return;
                }
                _fsm.jcmd = jcmd;
                _fsm.state = e_worker_state_read_data_len;
                break;

            case e_worker_state_read_data_len:
                if (evbuffer_get_length(input) < sizeof(data_len)) {
                    return;
                }
                if (__read_buffer(input, (char *)&data_len, sizeof(data_len))) {
                    loge("worker#%u read data_len failed. %s\n", g_worker_id, _fsm.cmd);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, read data_len failed");
                    return;
                }
                _fsm.data_len = data_len;
                if (data_len == 0) {
                    cJSON *jfileurl = cJSON_GetObjectItem(_fsm.jcmd, "fileurl");
                    if (!jfileurl || jfileurl->type != cJSON_String || strlen(jfileurl->valuestring) == 0) {
                        loge("worker#%u empty upload file and no fileurl. %s\n", g_worker_id, _fsm.cmd);
                        _fsm.state = e_worker_state_write_result;
                        __write_error(10008, NULL);
                        return;
                    }
                    _fsm.state = e_worker_state_calc;
                    __calc();
                    return;
                }
                _fsm.data = malloc(data_len);
                if (!_fsm.data) {
                    loge("worker#%u malloc data failed. %s\n", g_worker_id, _fsm.cmd);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, malloc data failed");
                    return;
                }
                memset(_fsm.data, 0, data_len);
                _fsm.state = e_worker_state_read_data;
                break;

            case e_worker_state_read_data:
                if (evbuffer_get_length(input) < _fsm.data_len) {
                    return;
                }
                if (__read_buffer(input, _fsm.data, _fsm.data_len)) {
                    loge("worker#%u read data failed. %s\n", g_worker_id, _fsm.cmd);
                    _fsm.state = e_worker_state_write_result;
                    __write_error(10011, "internal error, read data failed");
                    return;
                }
                _fsm.state = e_worker_state_calc;
                __calc();
                return;

            case e_worker_state_calc:
                loge("worker#%u recv data when calc\n", g_worker_id);
                if (evbuffer_drain(input, evbuffer_get_length(input)) < 0) {
                    loge("worker#%u evbuffer_drain failed\n", g_worker_id);
                    return;
                }
                break;

            case e_worker_state_write_result:
                loge("worker#%u recv data when write result\n", g_worker_id);
                if (evbuffer_drain(input, evbuffer_get_length(input)) < 0) {
                    loge("worker#%u evbuffer_drain failed\n", g_worker_id);
                    return;
                }
                break;

            default:
                loge("worker#%u in wrong state: %d\n", g_worker_id, _fsm.state);
                break;
        }
    }
}

void worker_run() {
    usleep(1000);

    memset(&_fsm, 0, sizeof(_fsm));
    _fsm.state = e_worker_state_idle;

    logi("worker#%u loop\n", g_worker_id);
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
    //goto _E; // !!! need test this in handle_eval.c 
    if (_fsm.state != e_worker_state_idle) {
        loge("worker#%u got a connection when non-idle, then disconnect\n", g_worker_id);
        goto _E;
    }
    logi("worker#%u got a connection\n", g_worker_id);

    struct bufferevent *bev = bufferevent_socket_new(g_worker_base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        loge("worker#%u bufferevent_socket_new failed, then disconnect\n", g_worker_id);
        goto _E;
    }
    _fsm.bev = bev;
    bufferevent_setcb(bev, __worker_readcb, __worker_writecb, __worker_eventcb, NULL);
    bufferevent_enable(bev, EV_WRITE);
    bufferevent_enable(bev, EV_READ);

    __reset_fsm_data();
    _fsm.state = e_worker_state_read_cmd_len;
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
