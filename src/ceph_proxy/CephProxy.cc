#include "CephProxyInterface.h"
#include "CephProxy.h"
#include "PoolContext.h"
#include "RadosWrapper.h"
#include "CephProxyOp.h"
#include "RadosWorker.h"
#include "ConfigRead.h"
#include "CephProxyLog.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <string.h>

#include <iostream>
#include <string>
#include <vector>

CephProxy* CephProxy::instance = nullptr;

#ifdef __linux__
#include <sys/syscall.h>
#endif

static pid_t CephProxyGetPid()
{
#ifdef __linux__
    return syscall(SYS_gettid);
#else
    return -ENOSYS;
#endif
}

int CephProxy::Init(const std::string &cephConf,
		    const std::string &logPath, 
		    size_t wNum)
{
    int ret = 0;
    ret = ProxyConfigInit();
    if (ret < 0) {
	ProxyDbgLogErr("proxy config init failed: %d", ret);
	ProxyDbgLogWarn("cannot read config file, use default config!!!");
    }

    config.cephConfigFile = cephConf;
    config.logPath = logPath;
    config.workerNum = wNum;
    state = PROXY_INITING;

    ret = RadosClientInit(&radosClient, std::string(ProxyGetCephConf()));
    if ( ret < 0) {
	ProxyDbgLogErr("RadosClient Init failed: %d.", ret);
	return ret;
    }

    ret = ptable.Init();
    if ( ret != 0 ) {
	ProxyDbgLogErr("PoolCtxTable Init failed: %d", ret);
	RadosClientShutdown(radosClient);
	return ret;
    } 

    std::string coreNumber = ProxyGetCoreNumber();
    std::vector<uint32_t> vecCoreId;
    vecCoreId.resize(0);
    pid_t pid = CephProxyGetPid();
    uint32_t bindCore = ProxyGetBindCore();
    if (pid != -ENOSYS && bindCore == 1) {
	const char *delim = ",";
	std::unique_ptr<char[]> tmp = std::make_unique<char[]>(coreNumber.size() + 1);
	strcpy(tmp.get(), coreNumber.c_str());
	char *p;
	char *savep;
	p = strtok_r(tmp.get(), delim, &savep);
	while (p) {
	    vecCoreId.push_back(atoi(p));
	    p = strtok_r(nullptr, delim, &savep);
	}
	RadosBindMsgrWorker(vecCoreId, pid);
    }

    worker = new(std::nothrow) RadosWorker(this);
    if (worker == nullptr) {
	ProxyDbgLogErr("Allocate memory failed.");
	return -1;
    }
    worker->Start(vecCoreId);

    state = PROXY_ACTIVE;
    return ret;
}

void CephProxy::Shutdown() {

    if (worker) {
	worker->Stop();
	delete worker;
	worker = nullptr;
    }

    ptable.Clear();
    RadosClientShutdown(radosClient);
    state = PROXY_DOWN;
}

int32_t CephProxy::Enqueue(ceph_proxy_op_t op, completion_t c)
{
    int32_t ret = 0;
    ret = worker->Queue(op, c);
    return ret;
}

rados_ioctx_t CephProxy::GetIoCtx(const std::string &pool)
{
    rados_ioctx_t ioctx = ptable.GetIoCtx(pool);
    if (ioctx == nullptr) {
        int ret = RadosCreateIoCtx(radosClient, pool ,&ioctx);
        if (ret != 0) {
	    ProxyDbgLogErr("Create IoCtx failed: %d", ret);
	    return nullptr;
	}

        ptable.Insert(pool, ioctx);
    }
    return ioctx;
}

rados_ioctx_t CephProxy::GetIoCtx2(const int64_t poolId)
{
    rados_ioctx_t ioctx = ptable.GetIoCtx(poolId);
    if (ioctx == nullptr) {
        int ret = RadosCreateIoCtx2(radosClient, poolId, &ioctx);
        if (ret != 0) {
            ProxyDbgLogErr("Create IoCtx failed: %d", ret);
	    return nullptr;
        }

        ptable.Insert(poolId, ioctx);
    }
    return ioctx;
}

int64_t CephProxy::GetPoolIdByPoolName(const char *poolName)
{
    rados_ioctx_t ioctx;
    int ret = RadosCreateIoCtx(radosClient, poolName, &ioctx);
    if (ret != 0) {
        ProxyDbgLogErr("Create IoCtx Failed: %d", ret);
        return -1;
    }
    
    ret = RadosGetPoolId(ioctx);
    if (ret < 0) {
        ProxyDbgLogErr("Get Pool ID Failed: %d", ret);
    }

    RadosReleaseIoCtx(ioctx);
    return ret;
}

int CephProxy::GetPoolNameByPoolId(int64_t poolId, char *buf, unsigned maxLen)
{
    rados_ioctx_t ioctx;
    int ret = RadosCreateIoCtx2(radosClient, poolId, &ioctx);
    if (ret != 0) {
        ProxyDbgLogErr("Create IoCtx Failed: %d", ret);
        return -1;
    }
    
    ret = RadosGetPoolName(ioctx, buf, maxLen);
    if (ret < 0) {
        ProxyDbgLogErr("Get Pool ID Failed: %d", ret);
    }

    RadosReleaseIoCtx(ioctx);
    return ret;
}

int64_t CephProxy::GetPoolIdByCtx(rados_ioctx_t ioctx)
{
    int ret = RadosGetPoolId(ioctx);
    if (ret < 0) {
        ProxyDbgLogErr("Get Pool ID Failed: %d", ret);
    }

    return ret;
}    

int CephProxy::GetClusterStat(CephClusterStat *stat)
{
	return RadosGetClusterStat(radosClient, stat);
}

int CephProxy::GetPoolStat(rados_ioctx_t ctx, CephPoolStat *stat)
{
	return RadosGetPoolStat(radosClient, ctx, stat);
}

int CephProxy::GetMinAllocSize(uint32_t *minAllocSize, CEPH_BDEV_TYPE_E type)
{
     switch(type) {
	case CEPH_BDEV_HDD:
     	    return RadosGetMinAllocSizeHDD(radosClient,minAllocSize);
	case CEPH_BDEV_SSD:
     	    return RadosGetMinAllocSizeSSD(radosClient,minAllocSize);
	default:
     	    return RadosGetMinAllocSizeHDD(radosClient,minAllocSize);
    }

    return 0;
}

CephProxyState CephProxy::GetState() const {
	return state;
}


