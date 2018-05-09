#ifndef _CPUNODE_HTTP_CALLBACK_H
#define _CPUNODE_HTTP_CALLBACK_H

#include <event2/event.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_post (
        struct event_base *base, 
        const char *url,
        const char *post,
        size_t post_size,
        void (*cb)(int result, char *data, unsigned int size),
        void *user_data
    );

#ifdef __cplusplus
}
#endif

#endif
