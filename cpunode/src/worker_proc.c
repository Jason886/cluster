#include "process_pool.h"

extern struct event_base *g_base;

int worker_proc(process_pool_t * pool) {
    event_reinit(g_base);
    usleep(1000);
    printf("worker#%u setup\n", pool->process_idx);
    while(1) {
        int i = 5;
        i += 16;
        if (i > 30000) {
            i = 0;
        }
    }
    sleep(10 + pool->process_idx*2);
    printf("worker#%u exit\n", pool->process_idx);
    return 0;
}
