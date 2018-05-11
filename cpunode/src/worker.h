#ifndef _CPUNODE_WORKER_H_
#define _CPUNODE_WORKER_H_

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/listener.h>

#ifdef __cplusplus
extern "C" {
#endif

void worker_run();

void worker_listener_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data);

void worker_listener_error_cb(struct evconnlistener *listener, void *ctx);

extern unsigned int g_worker_id;  // 工作进程编号, 从1开始, 0代表主进程
extern struct event_base *g_worker_base;
extern struct evconnlistener * g_worker_listener;

#ifdef __cplusplus
extern "C" {
#endif

#endif
