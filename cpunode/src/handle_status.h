#ifndef _CPUNODE_HANDLE_STATUS_H
#define _CPUNODE_HANDLE_STATUS_H

#include <event2/event.h>
#include <event2/http.h>

#ifdef __cplusplus
extern "C" {
#endif

void cpunode_handle_status(struct evhttp_request *req, void *arg);

#ifdef __cplusplus
}
#endif

#endif
