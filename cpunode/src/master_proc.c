#include "process_pool.h"
#include "global.h"

int master_proc(process_pool_t * pool) {
    usleep(100000);

    event_base_dispatch(g_base);
    
    return 0;
}
