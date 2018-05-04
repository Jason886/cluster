/*
 * author: xianchen.peng
 * date: 2018-05-04
 * desc: HTTP服务 
 */

#ifndef _CPUNODE_HTTPD_H_
#define _CPUNODE_HTTPD_H_

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int cpunode_httpd_init(struct config *conf, int port);
void cpunode_httpd_dispatch();

#ifdef __cplusplus
}
#endif

#endif
