#ifndef WTK_VIPKID_ENGINE_H
#define WTK_VIPKID_ENGINE_H

//#include "wtk_fstrec.h"
//#include "wtk_fstrec_cfg.h"
//#include "wtk_vad2.h"
#include "wtk_vipkid_engine_cfg.h"

typedef struct wtk_vad2 wtk_vad2_t;
typedef struct wtk_fstrec wtk_fstrec_t;
typedef struct wtk_queue wtk_queue_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wtk_vipkid_engine
{
	wtk_vipkid_engine_cfg_t* cfg;
	wtk_vad2_t *vad;
	wtk_fstrec_t* fstrec;
	wtk_queue_t *vqueue_raw;
	char *res;
	float spd_t;
}wtk_vipkid_engine_t;

wtk_vipkid_engine_t* wtk_vipkid_engine_new(wtk_vipkid_engine_cfg_t*);
int wtk_vipkid_engine_start(wtk_vipkid_engine_t*);
int wtk_vipkid_engine_feed(wtk_vipkid_engine_t*,char*,int model);
int wtk_vipkid_engine_feed_wav(wtk_vipkid_engine_t*,const char*);
int wtk_vipkid_engine_feed_wav2(wtk_vipkid_engine_t* engine,char *data,int len,int is_end);
int wtk_vipkid_engine_feed_feature(wtk_vipkid_engine_t*, const char*);
int wtk_vipkid_engine_feed_feautre2(wtk_vipkid_engine_t*, char*, int);
int wtk_vipkid_engine_reset(wtk_vipkid_engine_t*);
void wtk_vipkid_engine_delete(wtk_vipkid_engine_t*);

#ifdef __cplusplus
};
#endif
#endif
