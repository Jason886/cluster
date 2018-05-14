#ifndef _CPUNODE_HTTP_CALLBACK_H
#define _CPUNODE_HTTP_CALLBACK_H

#include <event2/event.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_post (
        struct event_base *base, 
        const char *url,
        const char *data,
        size_t size,
        void (*cb)(int result, char *data, unsigned int size, void *user_data), // note: callback中返回的data，你不需要释放。如果需要保存data里的数据，请拷贝出.
        void *user_data
    );

#ifdef __cplusplus
}
#endif

#endif
