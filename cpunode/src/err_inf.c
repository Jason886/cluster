#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "liblog.h"
#include "err_inf.h"

struct err_cfg {
    const short errno;
    const char *info;
};

static struct err_cfg errors[] = {
    { 0, "OK"},
    { 10001, "un-supported http method"},
    { 10002, "un-supported content type"},
    { 10003, "no token"},
    { 10004, "no appkey"},
    { 10005, "fileurl not begin with http"},
    { 10006, "no secretkey"},
    { 10007, "auth failed"},
    { 10008, "empty upload file and no fileurl"},
    { 10009, ""},
    { 10010, "no callback"},
    { 10011, "internal error"},
    { 10012, "invalid fea data"},
    { 10013, "download file empty from fileurl"},
    { 10014, "callback not begin with http"},
};

#define ERR_NUM sizeof(errors)/sizeof(struct err_cfg)

const char *cpunode_errmsg(short errno) {
    int i = 0;
    for (i = 0; i < ERR_NUM; i++) {
        if (errors[i].errno == errno) {
            return errors[i].info;
        }
    }
    return "unknown error";
}


