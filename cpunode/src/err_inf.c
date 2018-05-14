#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "liblog.h"
#include "err_inf.h"

/*
 * 1XXX: request params error
 * 2XXX: vipkid engine error 
 * 3XXX: analyzer error
 * 4XXX: other error
 */

#define UNKNOWN_ERROR "Unknown error."

struct err_cfg {
    const short errno;
    const char logflag;
    const char *fmt;
};

static struct err_cfg errors[] = {
    { 0, 0, "OK"},

    { 10001, 1, "un-supported http method"},
    { 10002, 1, "un-supported content type"},
    { 10003, 1,  "no token"},
    { 10004, 1,  "no appkey"},
    { 10005, 1,  "fileurl not begin with http"},
    { 10006, 1,  "no secretkey"},
    { 10007, 1, "auth failed"},
    { 10008, 1, "empty upload file and no fileurl"},
    { 10009, 1, ""},
    { 10010, 1, "no callback"},
    { 10011, 1, "internal error"},
    { 10012, 1, "invalid fea data"},
    { 10013, 1, "download file empty from fileurl"},
    { 10014, 1, "callback not begin with http"},


    { 20001, 1, "invalid request"},

    { 1010, 1, "the uploaded file data is invalid."},
    { 1011, 1, "the specific file not uploaded."},

    { 2001, 1, "wtk_vipkid_engine_new() failed."},
    { 2002, 1, "wtk_vipkid_engine_start() failed."},
    { 2010, 1, "wtk_vipkid_engine_feed_feautre2() failed."},
    { 2030, 1, "result of 'wtk_vipkid_engine_feed_feautre2' is null."},
    { 2040, 1, "result of 'wtk_vipkid_engine_feed_feautre2' is not valid json: %s ..."},
    { 2050, 1, "json field not found in result of 'wtk_vipkid_engine_feed_feautre2': %s ..."},
    
    { 3001, 1, "the vidkid engine cfg not loaded."},
    { 3010, 1, "unsupported compress flag: %d."},
    { 3011, 1, "decompress faild. compress flag=%d."},
};

#define ERR_NUM sizeof(errors)/sizeof(struct err_cfg)

#define _ERR_INF_LOG_ERR \
if(errno && logflag) { loge("errno = %d, errinfo = %s", errno, ei ? ei->info : info); }

void err_set(short errno, struct errinf * ei, ...) {
    char info[ERR_INF_SIZE], logflag = 1;
    va_list ap;
    size_t i;

    if(ei) ei->errno = errno;

    for(i = 0; i < ERR_NUM; i++) {
        if(errors[i].errno == errno) {
            logflag = errors[i].logflag;
            va_start(ap, ei);
            vsnprintf(ei? ei->info: info, ERR_INF_SIZE, errors[i].fmt, ap);
            va_end(ap);
            goto end;
        }
    }

    snprintf(ei? ei->info: info, ERR_INF_SIZE, UNKNOWN_ERROR);

end:
    _ERR_INF_LOG_ERR;
}

const char *cpunode_errmsg(short errno) {
    int i = 0;
    for (i = 0; i < ERR_NUM; i++) {
        if (errors[i].errno == errno) {
            return errors[i].fmt;
        }
    }
    return "unknown error";
}


