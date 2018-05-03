#ifndef WTK_VIPKID_ENGINE_CFG_H
#define WTK_VIPKID_ENGINE_CFG_H

//#include "wtk_vad2_cfg.h"
//#include "wtk_fstrec_cfg.h"

typedef struct wtk_vad2_cfg wtk_vad2_cfg_t;
typedef struct wtk_fstrec_cfg wtk_fstrec_cfg_t;
typedef struct wtk_cfg_file wtk_cfg_file_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wtk_vipkid_engine_cfg
{
	wtk_vad2_cfg_t *vad_cfg;
	wtk_fstrec_cfg_t* fstrec_cfg;
	wtk_cfg_file_t* cfg_file;
	unsigned int  use_bin:1;
	unsigned int  use_vad:1;
	unsigned int use_stream:1;
}wtk_vipkid_engine_cfg_t;


wtk_vipkid_engine_cfg_t* wtk_vipkid_engine_cfg_init(char*,int);
int wtk_vipkid_engine_cfg_clean(wtk_vipkid_engine_cfg_t*);

#ifdef __cplusplus
};
#endif
#endif
