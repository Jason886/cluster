#include "process_pool.h"

int master_proc(process_pool_t * pool) {
    usleep(100000);
    while(1) {
        int i = 5;
        i += 16;
        if (i > 30000) {
            i = 0;
        }
    }
    return 0;
}
