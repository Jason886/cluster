/*
 * author: xianchen.peng
 * date: 2018-05-04
 * desc: HTTP服务 
 */

#ifndef _CPUNODE_HTTPD_H_
#define _CPUNODE_HTTPD_H_

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cpunode_httpd {
    struct evhttp *http;
    struct evhttp_bound_socket * socket; 
} cpunode_httpd_t;

int cpunode_httpd_init(struct config *conf, int port);
void cpunode_httpd_free();

#ifdef __cplusplus
}
#endif

#endif
