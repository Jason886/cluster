#ifndef _CPUNODE_HANDLE_EVAL_H_
#define _CPUNODE_HANDLE_EVAL_H_

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

void cpunode_handle_eval(struct evhttp_request *req, void *path);

#ifdef __cplusplus
}
#endif

#endif
