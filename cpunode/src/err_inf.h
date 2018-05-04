#ifndef _CPUNODE_ERR_INFO_H_
#define _CPUNODE_ERR_INFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#define ERR_INF_SIZE 256

struct errinf {
    short errno;
    char info[ERR_INF_SIZE];
};

void err_set(short errno, struct errinf * ei, ...);

#ifdef __cplusplus
}
#endif

#endif

