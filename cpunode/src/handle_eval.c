#include "handle_eval.h"
#include "global.h"
#include "worker_pool.h"

void cpunode_handle_eval(struct evhttp_request *req, void *path) {
    printf("hihihi idx = %u\n", g_worker_pool->idx);
}
