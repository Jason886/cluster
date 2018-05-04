/*
 * author: xianchen.peng
 * date: 2018-05-04
 * desc: 全局变量
 */

#ifndef _CPUNODE_GLOBAL_H_
#define _CPUNODE_GLOBAL_H_

#include "liblog.h"
#include "libconfig.h"
#include "libmacro.h"

#include "wtk_vipkid_engine.h"
#include "process_pool.h"

#include <event2/event.h>
#include <event2/http.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct config *g_conf;
extern wtk_vipkid_engine_cfg_t *g_vipkid_engine_cfg;
extern struct event_base *g_base;
extern process_pool_t g_worker_pool;
extern int g_port;

int g_init_log(struct config *conf);
int g_load_vipkid_engine_cfg(struct config *conf);
void g_unload_vipkid_engine_cfg();

#ifdef __cplusplus
}
#endif

#endif
