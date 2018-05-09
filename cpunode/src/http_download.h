#ifndef _CPUNODE_HTTP_DOWNLOAD_H
#define _CPUNODE_HTTP_DOWNLOAD_H

#include <event2/event.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_download_start (
        struct event_base *base, 
        const char *host,
        unsigned int port,
        const char *uri,
        void (*cb)(int result, char *data, unsigned int size),
        void *user_data
    );

#ifdef __cplusplus
}
#endif

#endif
