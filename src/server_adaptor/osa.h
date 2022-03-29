/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef SA_O_H
#define SA_O_H

#include "sa_def.h"

#include <map>

#ifdef __cplusplus
extern "C" {
#endif
class SaExport;
int OSA_Init(SaExport &sa);
int OSA_Finish();
int OSA_FinishCacheOps(void *p, int r);
void OSA_ProcessBuf(const char *buf, unsigned int len, int cnt, void *p);

void OSA_EncodeOmapGetkeys(const SaBatchKeys *batchKeys, int i, void *p);
void OSA_EncodeOmapGetvals(const SaBatchKv *KVs, int i,void *p);
void OSA_EncodeOmapGetvalsbykeys(const SaBatchKv *keyValue, int i, void *p);
void OSA_EncodeRead(uint64_t opType, unsigned int offset, unsigned int len, char *buf, unsigned int bufLen, int i,
	       	void *p);
void OSA_SetOpResult(int i, int32_t ret, void *p);
void OSA_EncodeXattrGetxattr(const SaBatchKv *keyValue, int i, void *p);
void OSA_EncodeXattrGetxattrs(const SaBatchKv *keyValue, int i, void *p);
void OSA_EncodeGetOpstat(uint64_t psize, time_t ptime, int i, void *p);
int OSA_ExecClass(SaOpContext *pctx, PREFETCH_FUNC prefetch);
void OSA_EncodeListSnaps(const ObjSnaps *objSnaps, int i, void *p);

#ifdef __cplusplus
}
#endif

#endif
