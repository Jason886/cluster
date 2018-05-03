#include "work_pool.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <malloc.h>
#include <stdio.h>

#define SIZE_NAME_NORMAL 128
#define SIZE_NAME_LONG 512


typedef struct process
{
    char name[SIZE_NAME_NORMAL];
    pid_t pid;
    int pipefd[2];
    size_t score;
} process_t;

typedef struct instance {
    char prg_name[SIZE_NAME_LONG];
    char cfg_name[SIZE_NAME_LONG];

    u_int16_t process_num;
    u_int16_t process_idx;

    struct process *proc;
} instance_t;

#define __offset(pinst) ((pinst)->proc[pinst->process_idx])
#define __round_robin(pinst, roll) \
    ((pinst)->proc[((roll)%(pinst)->process_num)+1].pipefd[1])

static u_int8_t g_enable;
static void __sig_quit(int sig) {
    g_enable = 0;
}

static int __master(instance_t * pinst) {
    int ret = 0;
    int fd = 0;
    int ix = 0;
    int roll = 0;
    char c = 0;

    printf("Master#%u setup\n", pinst->process_idx);

    for (g_enable=1; g_enable; ) {
        fd = __round_robin(pinst, ++roll);

        c = 'A' + roll %3;
        ret = write(fd, &c, 1);
        if (ret <= 0) {
            return -1;
        }
        sleep(1);
    }

    /* tell the workers to quit */
    for (ix = 1; ix <= pinst->process_num; ++ix) {
        c = 'Q';
        write(__round_robin(pinst, ++roll), &c, 1);
    }

    printf("Master#%u shutdown\n", pinst->process_idx);
    return 0;
}

static int __worker(instance_t *pinst) {
    int fd = __offset(pinst).pipefd[0];
    int ix = 0;
    ssize_t read_byte = -1;
    char buffer[1024] = {0};

    printf("Worker#%u setup\n", pinst->process_idx);
    for (g_enable = 1; g_enable; ) {
        read_byte = read(fd, buffer, sizeof(buffer));
        if (read_byte <= 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return -1;
        }

        for (ix = 0; ix< read_byte; ++ix) {
            switch(buffer[ix]) {
                case 'A':
                case 'B':
                case 'C':
                    __offset(pinst).score += buffer[ix];
                    printf("Worker#%u Recv command: %c, score: %llu\n", pinst->process_idx, buffer[ix], __offset(pinst).score);
                    break;
                case 'Q':
                    printf("Quit\n");
                    g_enable = 0;
                    break;
                default:
                    break;
            }
        }
    }

    printf("Worker#%u shutdown\n", pinst->process_idx);
    return 0;
}


int process_pool(instance_t *pinst, u_int16_t process_num) {
    int ret = -1;
    int ix = 0;
    int status = 0;

    if( !pinst || !process_num) {
        printf("NULL\n");
        goto _E1;
    }

    signal(SIGINT, __sig_quit);
    signal(SIGTERM, __sig_quit);

    pinst->process_idx = 0;
    pinst->process_num = process_num;
    pinst->proc = (process_t *)calloc(process_num+1, sizeof(process_t));
    if (!pinst->proc) {
        printf("Alloc process pool struct failed\n");
        goto _E1;
    }

    for (ix = 1; ix <= process_num; ix++) {
        int bufsize = 1;
        ret = pipe(pinst->proc[ix].pipefd);
        if (0 != ret) {
            printf("socketpair\n");
            goto _E2;
        }

        printf("Setup worker#%u\n", ix);

        pinst->proc[ix].pid = fork();
        if (pinst->proc[ix].pid < 0) {
            printf("fork\n");
            goto _E2;
        }
        else if (pinst->proc[ix].pid > 0) {
            // father
            close(pinst->proc[ix].pipefd[0]);
            continue;
        }
        else {
            // child
            close(pinst->proc[ix].pipefd[1]);
            pinst->process_idx = ix;
            ret = __worker(pinst);
            goto _E2;
        }
    }

    ret = __master(pinst);

    for (ix = 1; ix <= pinst->process_num; ix++) {
        waitpid(pinst->proc[ix].pid, &status, WNOHANG);
    }

_E2:
    for (ix = 1; ix<=pinst->process_num; ix++) {
        close(pinst->proc[ix].pipefd[1]);
        close(pinst->proc[ix].pipefd[0]);
    }
    free(pinst->proc);
_E1:
    return ret;
}

int main() {
    instance_t inst = {0};

    return process_pool(&inst, 5);
}
