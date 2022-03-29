/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltf All rights reserved.
 *
 */

#ifndef PROXY_RADOS_MONITOR_H
#define PROXY_RADOS_MONITOR_H

#include <stdint.h>
#include <vector>
#include <regex>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include "CcmAdaptor.h"
#include "CephProxy.h"

#define KIB_NUM (1024ULL)
#define MIB_NUM (KIB_NUM * 1024ULL)
#define GIB_NUM (MIB_NUM * 1024ULL)
#define TIB_NUM (GIB_NUM * 1024ULL)
#define PIB_NUM (TIB_NUM * 1024ULL)

#define DEFAULT_UPDATE_TIME_INTERVAL 3

typedef struct {
	uint64_t storedSize;
	uint64_t objectsNum;
	uint64_t usedSize;
	float useRatio;
	uint64_t maxAvail;
	uint64_t version;
} PoolUsageInfo;

class CephProxy;

class PoolUsageStat {
private:
	std::shared_mutex mutex;
	CephProxy *proxy;
	std::thread timer;
	ClusterManagerAdaptor *ccm_adaptor;
	bool stop;
	uint32_t timeInterval;
	std::map<uint32_t, PoolUsageInfo> poolInfoMap;
	bool poolNewNotifyFnFirstRegistered;
	std::map<uint32_t, PoolUsageInfo> tmpPoolInfoMap;
	std::vector<uint32_t> tempPoolList;

	int32_t FirstReportAllPool(void);
	int32_t ReportPoolNewAndDel(std::vector<uint32_t> &newPools,
								std::vector<uint32_t> &delPools);
public:
	PoolUsageStat(CephProxy *_proxy): proxy(_proxy), ccm_adaptor(NULL), stop(false),
					timeInterval(DEFAULT_UPDATE_TIME_INTERVAL),
					poolNewNotifyFnFirstRegistered(false) { }
	~PoolUsageStat() { }

	int32_t GetPoolUsageInfo(uint32_t poolId, PoolUsageInfo *poolInfo);
	int32_t GetPoolAllUsedAndAvail(uint64_t &usedSize, uint64_t &maxAvail);
	int32_t Record(std:: smatch &result);
	int32_t GetPoolReplicationSize(uint32_t poolId, double &rep);
	int32_t UpdatePoolUsage(void);
	int32_t UpdatePoolList(void);
	int32_t RegisterPoolDelNotifyFn(NotifyPoolEventFn fn);
	int32_t RegisterPoolNewNotifyFn(NotifyPoolEventFn fn);
	uint32_t GetTimeInterval();
	bool isStop();
	void Start(void);
	void Stop();
};

#endif
