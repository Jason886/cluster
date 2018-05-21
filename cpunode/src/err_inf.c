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
};

#define ERR_NUM sizeof(errors)/sizeof(struct err_cfg)

const char *cpunode_errmsg(short errno) {
    int i = 0;
    for (i = 0; i < ERR_NUM; i++) {
        if (errors[i].errno == errno) {
            return errors[i].fmt;
        }
    }
    return "unknown error";
}


