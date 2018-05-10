#ifndef _CPUNODE_HANDLE_EVAL_H_
#define _CPUNODE_HANDLE_EVAL_H_

#include <event2/event.h>
#include <event2/http.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct event *g_eval_timer;
extern struct timeval g_eval_timeval;

void cpunode_handle_eval(struct evhttp_request *req, void *arg);
void eval_timer_cb(evutil_socket_t fd, short what, void *arg);

u_int16_t get_free_worker();

#ifdef __cplusplus
}
#endif

#endif
