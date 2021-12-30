/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltf All rights reserved.
 *
 */

#include <vector>
#include <regex>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <cstring>
#include <string>
#include <unistd.h>

#include "CephProxyLog.h"
#include "RadosMonitor.h"

using namespace std;
using namespace librados;

std::string poolNamePattern = "\\s*(\\w*)\\s*";
std::string poolIdPattern   = "(\\d+)\\s+";
std::string storedPattern    = "([0-9]+\\.?[0-9]*)\\s(TiB|GiB|MiB|KiB|B)*\\s*";
std::string objectsPattern  = "([0-9]+\\.?[0-9]*)(k|M)*\\s*";
std::string usedPattern     = "([0-9]*\\.?[0-9]*)\\s(TiB|GiB|MiB|KiB|B)*\\s*";
std::string usedRatioPattern = "([0-9]*\\.?[0-9]*)\\s*";
std::string availPattern    = "([0-9]*\\.?[0-9]*)\\s(TiB|GiB|MiB|KiB|B).*";

uint64_t TransStrUnitToNum(const char *strUnit)
{
    if (strcmp("PiB", strUnit) == 0) {
        return PIB_NUM;
    } else if (strcmp("TiB", strUnit) == 0) {
        return TIB_NUM;
    } else if (strcmp("GiB", strUnit) == 0) {
        return GIB_NUM;
    } else if (strcmp("MiB", strUnit) == 0) {
        return MIB_NUM;
    } else if (strcmp("KiB", strUnit) == 0) {
        return KIB_NUM;
    } else if (strcmp("B", strUnit) == 0) {
        return 1;
    }

    return 1;
}

int32_t PoolUsageStat::GetPoolUsageInfo(uint32_t poolId, PoolUsageInfo *poolInfo)
{
    if (poolInfo == NULL) {
        ProxyDbgLogErr("poolInfo is NULL");
	return -22;
    }

    mutex.lock_shared();
    std::map<uint32_t, PoolUsageInfo>::iterator iter = poolInfoMap.find(poolId);
    if (iter == poolInfoMap.end()) {
        mutex.unlock_shared();
	return -1;
    }

    poolInfo->storedSize = iter->second.storedSize;
    poolInfo->objectsNum = iter->second.objectsNum;
    poolInfo->usedSize = iter->second.usedSize;
    poolInfo->useRatio = iter->second.useRatio;
    poolInfo->maxAvail = iter->second.maxAvail;

    mutex.unlock_shared();
    return 0;
}


int32_t PoolUsageStat::GetPoolAllUsedAndAvail(uint64_t &usedSize, uint64_t &maxAvail)
{
    usedSize = 0;
    maxAvail = 0;
    mutex.lock_shared();
    std::map<uint32_t, PoolUsageInfo>::iterator iter = poolInfoMap.begin();
    for (; iter != poolInfoMap.end(); iter++) {
        usedSize += iter->second.usedSize;
	maxAvail = iter->second.maxAvail;
    }
    mutex.unlock_shared();

    return 0;
}

int32_t PoolUsageStat::GetPoolReplicationSize(uint32_t poolId, double &rep)
{
    rados_ioctx_t ioctx = proxy->GetIoCtx2(poolId);
    if (ioctx == NULL) {
        ProxyDbgLogErr("get ioctx failed.");
	return -1;
    }

    CephPoolStat stat;
    int32_t ret = proxy->GetPoolStat(ioctx, &stat);
    if (ret != 0) {
        ProxyDbgLogErr("Get ioctx failed.");
	return -1;
    }

    rep = (stat.numObjectCopies * 1.0) / stat.numObjects;
    return 0;
}

int32_t PoolUsageStat::Record(std::smatch &result)
{
    uint32_t poolId = 0;
    double storedSize = 0.0;
    uint64_t storedSizeUnit = 1;
    double objectsNum = 0.0;
    uint64_t numUnit = 1;
    double usedSize = 0.0;
    uint64_t usedSizeUnit = 1;
    double maxAvail = 0.0;
    uint64_t maxAvailUnit = 1;
    double useRatio = 0.0;

    //
    for (size_t i = 2; i < result.size(); i++) {
        if (i == 2) {
	    int id = atoi(result[i].str().c_str());
	    if (id < 0) {
	        return -1;
	    }
	    poolId = (uint32_t)id;
	} else if (i == 3) {
	    storedSize = atof(result[i].str().c_str());
	} else if (i == 4) {
	    storedSizeUnit = TransStrUnitToNum(result[i].str().c_str());
	} else if (i == 5) {
	    objectsNum = atof(result[i].str().c_str());
	} else if (i == 6) {
	    if (result[i].length() == 0) {
	        numUnit = 1;
	    } else if (strcmp(result[i].str().c_str(), "k") == 0) {
	        numUnit = 1000;
	    } else if (strcmp(result[i].str().c_str(), "m") == 0) {
	        numUnit = 1000 * 1000;
	    }
	} else if (i == 7) {
	    usedSize = atof(result[i].str().c_str());
	} else if (i == 8) {
	    usedSizeUnit = TransStrUnitToNum(result[i].str().c_str());
	} else if (i == 9) {
	    useRatio = atof(result[i].str().c_str());
	} else if (i == 10) {
	    maxAvail = useRatio=atof(result[i].str().c_str());
	} else if (i == 11) {
	    maxAvailUnit = TransStrUnitToNum(result[i].str().c_str());
	}
    }

    double rep = 0.0;
    int32_t ret = GetPoolReplicationSize(poolId, rep);
    if (ret != 0) {
        ProxyDbgLogErr("get replicaiton size failed.");
	return -1;
    }

    mutex.lock();
    poolInfoMap[poolId].storedSize = (uint64_t)(storedSize * storedSizeUnit);
    poolInfoMap[poolId].objectsNum = (uint64_t)(objectsNum * numUnit);
    poolInfoMap[poolId].usedSize   = (uint64_t)(usedSize * usedSizeUnit);
    poolInfoMap[poolId].useRatio   = useRatio;
    poolInfoMap[poolId].maxAvail   = (uint64_t)(maxAvail * maxAvailUnit * rep);
    mutex.unlock();
    return 0;
}

static int GetPoolStorageUsage(PoolUsageStat *mgr, const char *input)
{
    std::string pattern = poolNamePattern + poolIdPattern + storedPattern +
	                  objectsPattern + usedPattern + usedRatioPattern +
			  availPattern;

    std::vector<string> infoVector;
    char *strs = new char[strlen(input) + 1];
    strcpy(strs, input);

    char *p = strtok(strs, "\n");
    while (p) {
        string s = p;
	infoVector.push_back(s);
	p = strtok(NULL, "\n");
    }

    for (size_t i = 0; i < infoVector.size(); i++) {
        std::regex expression(pattern);
	std::smatch result;
	bool flag = std::regex_match(infoVector[i], result, expression);
	if (flag) {
	    int32_t ret = mgr->Record(result);
	    if (ret != 0) {
	        ProxyDbgLogErr("record pool useage info failed.");
		break;
	    }
	}
    }

    if (strs != nullptr) {
	delete[] strs;
	strs = nullptr;
    }

    return 0;
}

int PoolUsageStat::UpdatePoolUsage()
{
    librados::Rados *rados = reinterpret_cast<librados::Rados *>(proxy->radosClient);
    std::string cmd("{\"prefix\":\"df\"}");
    std::string outs;
    bufferlist inbl;
    bufferlist outbl;

    int ret = rados->mon_command(cmd, inbl, &outbl, &outs);
    if (ret < 0) {
        ProxyDbgLogErr("get cluster stat failed: %d", ret);
        return ret;
    }

    ret = GetPoolStorageUsage(this, outbl.c_str());
    if (ret != 0) {
        ProxyDbgLogErr("get pool storage usage failed: %d", ret);
        return -1;
    }

    return 0;
}

bool PoolUsageStat::isStop()
{
    return stop;
}

uint32_t PoolUsageStat::GetTimeInterval()
{
    return timeInterval;
}


static void PoolUsageTimer(PoolUsageStat *poolUsageManager)
{
    while (true) {
        if (poolUsageManager->isStop()) {
	    break;
	}

	int32_t ret = poolUsageManager->UpdatePoolUsage();
        if (ret != 0) {
            ProxyDbgLogErr("update pool usage failed.");
	}

	sleep(poolUsageManager->GetTimeInterval());
    }
}

void PoolUsageStat::Start()
{
    int32_t ret = UpdatePoolUsage();
    if (ret != 0) {
        ProxyDbgLogErr("get pool usage failed: %d", ret);
    }

    timer = std::thread(PoolUsageTimer, this);
}

void PoolUsageStat::Stop()
{
    stop = true;
    timer.join();
}
