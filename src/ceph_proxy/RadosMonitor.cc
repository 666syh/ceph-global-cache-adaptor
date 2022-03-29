/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
 *
 */

#include <vector>
#include <regex>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <cstring>
#include <string>
#include <algorithm>
#include <unistd.h>

#include "CephProxyLog.h"
#include "RadosMonitor.h"

using namespace std;
using namespace librados;

std::string poolNamePattern = "\\s*(\\S*)\\s*";
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
    if (iter != poolInfoMap.end()) {
        maxAvail = iter->second.maxAvail;
    }
    for (; iter != poolInfoMap.end(); iter++) {
        usedSize += iter->second.usedSize;
        if (maxAvail == 0 && iter->second.maxAvail != 0) {
	        maxAvail = iter->second.maxAvail;
        }
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

    if (stat.numObjects == 0 || stat.numObjectCopies == 0) {
        rep = 3;
    } else {
        rep = (stat.numObjectCopies * 1.0) / stat.numObjects;
    }
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
	    maxAvail = atof(result[i].str().c_str());
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
    tmpPoolInfoMap[poolId].storedSize = (uint64_t)(storedSize * storedSizeUnit);
    tmpPoolInfoMap[poolId].objectsNum = (uint64_t)(objectsNum * numUnit);
    tmpPoolInfoMap[poolId].usedSize   = (uint64_t)(usedSize * usedSizeUnit);
    tmpPoolInfoMap[poolId].useRatio   = useRatio;
    tmpPoolInfoMap[poolId].maxAvail   = (uint64_t)(maxAvail * maxAvailUnit * rep);
    tempPoolList.push_back(poolId);
    mutex.unlock();
    return 0;
}

static int GetPoolStorageUsage(PoolUsageStat *mgr, const char *input)
{
    std::string pattern = poolNamePattern + poolIdPattern + storedPattern + objectsPattern + usedPattern +
	                  usedRatioPattern + availPattern;

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
            if (strs != nullptr) {
                delete[] strs;
                strs = nullptr;
            }
            return -1;
	    }
	}
    }

    if (strs != nullptr) {
	delete[] strs;
	strs = nullptr;
    }

    return 0;
}

int32_t PoolUsageStat::FirstReportAllPool(void)
{
    if (poolNewNotifyFnFirstRegistered == true) {
        std::vector<uint32_t> existsPools;
        std::map<uint32_t, PoolUsageInfo>::iterator iter = poolInfoMap.begin();
        for (; iter != poolInfoMap.end(); iter++) {
            existsPools.push_back(iter->first);
        }

        int32_t ret = ccm_adaptor->ReportCreatePool(existsPools);
        if (ret != 0) {
            ProxyDbgLogErr("report create pool failed.");
            return -1;
        }
        poolNewNotifyFnFirstRegistered = false;
    }

    return 0;
}

int32_t PoolUsageStat::ReportPoolNewAndDel(std::vector<uint32_t> &newPools, std::vector<uint32_t> &delPools)
{
    int32_t ret = ccm_adaptor->ReportCreatePool(newPools);
    if (ret != 0) {
        ProxyDbgLogErr("report create pool failed.");
        return -1;
    }

    ret = ccm_adaptor->ReportDeletePool(delPools);
    if (ret != 0) {
        ProxyDbgLogErr("report deleted pool failed.");
        return -1;
    }

    return 0;
}

int32_t PoolUsageStat::UpdatePoolList()
{
    mutex.lock();

    std::vector<uint32_t> deletedPool;
    std::vector<uint32_t> newPool;

    std::vector<uint32_t>::iterator tmpIter = tempPoolList.begin();
    for (; tmpIter != tempPoolList.end(); tmpIter++) {
        std::map<uint32_t, PoolUsageInfo>::iterator mapIter = poolInfoMap.find(*tmpIter);
        if (mapIter == poolInfoMap.end()) {
            newPool.push_back(*tmpIter);
        }
    }

    std::map<uint32_t, PoolUsageInfo>::iterator iter = poolInfoMap.begin();
    for (; iter != poolInfoMap.end(); iter++) {
        if (find(tempPoolList.begin(), tempPoolList.end(), iter->first) == tempPoolList.end()) {
            deletedPool.push_back(iter->first);
        }
    }

    for (size_t i = 0; i < tempPoolList.size(); i++) {
        uint32_t poolId = tempPoolList[i];
        poolInfoMap[poolId].storedSize = tmpPoolInfoMap[poolId].storedSize;
        poolInfoMap[poolId].objectsNum = tmpPoolInfoMap[poolId].objectsNum;
        poolInfoMap[poolId].usedSize = tmpPoolInfoMap[poolId].usedSize;
        poolInfoMap[poolId].useRatio = tmpPoolInfoMap[poolId].useRatio;
        poolInfoMap[poolId].maxAvail = tmpPoolInfoMap[poolId].maxAvail;
    }

    for (size_t i = 0; i < deletedPool.size(); i++) {
        uint32_t poolId = deletedPool[i];
        std::map<uint32_t, PoolUsageInfo>::iterator delIter = poolInfoMap.find(poolId);
        if (delIter != poolInfoMap.end()) {
            poolInfoMap.erase(delIter);
        }
    }
    tmpPoolInfoMap.clear();
    tempPoolList.clear();

    int32_t ret = FirstReportAllPool();
    if (ret != 0) {
        ProxyDbgLogErr("report all pool failed.");
        mutex.unlock();
        return -1;
    }

    ret = ReportPoolNewAndDel(newPool, deletedPool);
    if (ret != 0) {
        ProxyDbgLogErr("report pool new or del failed.");
        mutex.unlock();
        return -1;
    }

    mutex.unlock();
    return 0;
}

int PoolUsageStat::UpdatePoolUsage(void)
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

    ret = UpdatePoolList();
    if (ret != 0) {
        ProxyDbgLogErr("get pool storage usage failed: %d", ret);
        return -1;
    }

    return 0;
}

int PoolUsageStat::RegisterPoolDelNotifyFn(NotifyPoolEventFn fn)
{
    if (ccm_adaptor == NULL) {
        ProxyDbgLogErr("poolUsageStat is not inited.");
        return -1;
    }

    return ccm_adaptor->RegisterPoolDeleteReportFn(fn);
}

int PoolUsageStat::RegisterPoolNewNotifyFn(NotifyPoolEventFn fn)
{
    if (ccm_adaptor == NULL) {
        ProxyDbgLogErr("poolUsageStat is not inited.");
        return -1;
    }

    int ret = ccm_adaptor->RegisterPoolCreateReportFn(fn);
    if (ret != 0) {
        ProxyDbgLogErr("register poolCreateReport Fn failed");
        return -1;
    }

    mutex.lock_shared();
    poolNewNotifyFnFirstRegistered = true;
    mutex.unlock_shared();
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
    ccm_adaptor = new (std::nothrow) ClusterManagerAdaptor();
    if (ccm_adaptor == nullptr) {
        ProxyDbgLogErr("Allocate ClusterManagerAdaptor faild.");
        return;
    }
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
