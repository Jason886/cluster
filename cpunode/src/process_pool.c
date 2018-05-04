#include "process_pool.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>

int process_pool(process_pool_t *pool, u_int16_t process_num, u_int16_t process_max_used, master_f master, worker_f worker) {
    int ret = -1;
    int ix = 0;
    int status = 0;
    int pipe_count = 0;
    int fork_count = 0;

    if (!pool || process_num == 0) {
        goto _E1;
    }

    pool->process_idx = 0;
    pool->process_max_used = process_max_used;
    pool->process_num = process_num;
    pool->process = (process_t *)calloc(process_num+1, sizeof(process_t));
    if (!pool->process) {
        fprintf(stderr, "alloc process pool struct failed.\n");
        goto _E1;
    }
    memset(pool->process, 0, sizeof(process_t) * (process_num+1));

    for (ix = 1; ix <= process_num; ++ix) {
        pool->process[ix].used = 0;

        ret = pipe(pool->process[ix].pipefd);
        if (ret) {
            fprintf(stderr, "pipe failed\n");
            goto _E2;
        }
        pipe_count = ix;

        printf("fork worker#%u\n", ix);
        pool->process[ix].pid = fork();
        if (pool->process[ix].pid < 0) {
            printf("fork worker#%u failed\n", ix);
            goto _E2;
        }
        fork_count = ix;

        if (pool->process[ix].pid > 0) {
            // father
            close(pool->process[ix].pipefd[0]);
            continue; 
        } else {
            // child
            /*
            int iii = 0;
            for (iii = 1; iii <= pipe_count; ++iii) {
                close(pool->process[iii].pipefd[1]);
            }
            pool->process_idx = ix;
            */
            ret = worker(pool);
            exit(ret);
        }
    }

    ret = master(pool);
    for (ix = 1; ix <= pool->process_num; ++ix) {
        printf("wait worker#%u exit\n", ix);
        waitpid(pool->process[ix].pid, &status, 0);
        printf("worker#%u exited\n", ix);
    }

    printf("master exit\n");

_E2:
    for (ix = 1; ix <= pipe_count; ++ix) {
        close(pool->process[ix].pipefd[1]);
        if (ix > fork_count) {
            close(pool->process[ix].pipefd[0]);
        }
    }
    free(pool->process);

_E1:
    return ret;
}
