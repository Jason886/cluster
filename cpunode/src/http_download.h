#ifndef _CPUNODE_HTTP_DOWNLOAD_H
#define _CPUNODE_HTTP_DOWNLOAD_H

#include <event2/event.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_download (
        struct event_base *base, 
        const char *url,
        void (*cb)(int result, char *data, unsigned int size),  // note: callback中返回的data，由你来释放. 
        void *user_data
    );

#ifdef __cplusplus
}
#endif

#endif
