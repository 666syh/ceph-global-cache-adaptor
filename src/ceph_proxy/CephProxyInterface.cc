#include "CephProxyInterface.h"
#include "CephProxy.h"
#include "RadosWrapper.h"
#include "CephProxyOp.h"

#include <iostream>
#include <string>
#include <time.h>

int CephProxyInit(const char *conf, size_t wNum, const char *log, ceph_proxy_t *proxy)
{
    int ret = 0;
    std::string config(conf);
    std::string logPath(log);

    CephProxy *cephProxy = CephProxy::GetProxy();
    ret = cephProxy->Init(config, logPath, wNum);
    if (ret < 0) {
	std::cout << "CephProxy Init failed: " << ret << std::endl;
	*proxy = nullptr;
	return ret;
    }
    *proxy = cephProxy;
    return ret;
}

void CephProxyShutdown(ceph_proxy_t proxy)
{
    CephProxy *cephProxy = static_cast<CephProxy *>(proxy);
    cephProxy->Shutdown();
    cephProxy->instance = NULL;
    delete cephProxy;
    proxy = nullptr;
}

void CephProxyQueueOp(ceph_proxy_t proxy,rados_ioctx_t ioctx, ceph_proxy_op_t op, completion_t c)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy*>(proxy);
	cephProxy->Enqueue(ioctx, op, c);
}

rados_ioctx_t CephProxyGetIoCtx(ceph_proxy_t proxy, const char *poolname)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy *>(proxy);
	return cephProxy->GetIoCtx(poolname);
}
 
rados_ioctx_t CephProxyGetIoCtx2(ceph_proxy_t proxy, const int64_t poolId)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy *>(proxy);
	return cephProxy->GetIoCtx2(poolId);

}

int CephProxyGetClusterStat(ceph_proxy_t proxy, CephClusterStat *result)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy *>(proxy);
	return cephProxy->GetClusterStat(result);
}

int CephProxyGetPoolStat(ceph_proxy_t proxy, rados_ioctx_t io, CephPoolStat *stats)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy *>(proxy);
	return cephProxy->GetPoolStat(io, stats);
}

int CephProxyGetState(ceph_proxy_t proxy)
{
	CephProxy *cephProxy = reinterpret_cast<CephProxy *>(proxy);
	return (int)(cephProxy->GetState());
}

int CephProxyWriteOpInit(ceph_proxy_op_t *op, const char *pool, const char* oid)
{
	*op = RadosWriteOpInit(pool, oid);
	if (*op == nullptr) {
	    std::cout << "Create Write Op failed." << std::endl;
	    return -1;
	}
	return 0;
}

int CephProxyWriteOpInit2(ceph_proxy_op_t *op, const int64_t poolId, const char* oid)
{
	*op = RadosWriteOpInit2(poolId, oid);
	if (*op == nullptr) {
	    std::cout << "Create write Op failed." << std::endl;
	    return -1;
	}
	return 0;
}

void CephProxyWriteOpRelease(ceph_proxy_op_t op) {
	RadosWriteOpRelease(op);
}

void CephProxyWriteOpSetFlags(ceph_proxy_op_t op, int flags)
{
	RadosWriteOpSetFlags(op, flags);
}

void CephProxyWriteOpAssertExists(ceph_proxy_op_t op)
{
	RadosWriteOpAssertExists(op);
}

void CephProxyWriteOpAssertVersion(ceph_proxy_op_t op, uint64_t ver)
{
	RadosWriteOpAssertVersion(op, ver);
}

void CephProxyWriteOpCmpext(ceph_proxy_op_t op, const char *cmpBuf, size_t cmpLen, uint64_t off, int *prval)
{
	RadosWriteOpCmpext(op, cmpBuf, cmpLen, off ,prval);
}

void CephProxyWriteOpCmpXattr(ceph_proxy_op_t op,  const char *name, uint8_t compOperator, const char *value, size_t valLen)
{
	RadosWriteOpCmpXattr(op, name ,compOperator, value, valLen);
}

void CephProxyWriteOpOmapCmp(ceph_proxy_op_t op, const char *key, uint8_t compOperator, constchar *value, size_t valLen, int *prval)
{
	RadosWriteOpOmapCmp(op, key, compOperator, value, valLen, prval);
}

void CephProxyWriteOpSetXattr(ceph_proxy_op_t op, const char *name, const char *value, size_t valLen)
{
	RadosWriteOpSetXattr(op, name, value, valLen);
}

void CephProxyWriteOpRemoveXattr(ceph_proxy_op_t op, const char *name)
{
	RadosWriteOpRemoveXattr(op, name);
}

void CephProxyWriteOpCreateObject(ceph_proxy_op_t op, int exclusive, const char *category)
{
	RadosWriteOpCreateObject(op, exclusive, category);
}

void CephProxyWriteOpWrite(ceph_proxy_op_t op, const char *buffer, size_t len, uint64_t off)
{
	RadosWriteOpWrite(op, buffer, len, off);
}

void CephProxyWriteOpWriteSGL(ceph_proxy_op_t op, SGL_S *s, size_t len, uint64_t off)
{
	RadosWriteOpWriteSGL(op, s, len, off);
}

void CephProxyWriteOpWriteFull(ceph_proxy_op_t op, const char *buffer, size_t len)
{
	RadosWriteOpWriteFull(op, buffer, len);
}

void CephProxyWriteOpWriteFullSGL(ceph_proxy_op_t op, const SGL_S *s, size_t len)
{
	RadosWriteOpWriteFullSGL(op, s, len);
}

void CephProxyWriteOpWriteSame(ceph_proxy_op_t op, const char *buffer, size_t dataLen, size_t writeLen, uint64_t off)
{
	RadosWriteOpWriteSame(op, buffer, dataLen, writeLen, off);
}

void CephProxyWriteOpWriteSameSGL(ceph_proxy_op_t op, const SGL_S *sgl, size_t dataLen, size_t writeLen, uint64_t off)
{
	RadosWriteOpWriteSameSGL(op, sgl, dataLen, writeLen, off);
}

void CephProxyWriteOpAppend(ceph_proxy_op_t op, const char *buffer, size_t len)
{
	RadosWriteOpAppend(op, buffer, len);
}

void CephProxyWriteOpAppend(ceph_proxy_op_t op, const SGL_S *s, size_t len)
{
	RadosWriteOpAppendSGL(op, s, len);
}

void CephProxyWriteOpRemove(ceph_proxy_op_t op)
{
	RadosWriteOpRemove(op);
}

void CephProxyWriteOpTruncate(ceph_proxy_op_t op,  uint64_t off)
{
	RadosWriteOpTruncate(op, off);
}

void CephProxyWriteOpZero(ceph_proxy_op_t op,  uint64_t off, uint64_t len)
{
	RadosWriteOpZero(op, off, len);
}

void CephProxyWriteOpOmapSet(ceph_proxy_op_t op, char const* const* keys, char const* const* vals, const size_t *lens, size_t num)
{
	RadosWriteOpOmapSet(op, keys, vals, lens, num);
}

void CephProxyWriteOpOmapRmKeys(ceph_proxy_op_t op, char const* const* keys, size_t keysLen)
{
	RadosWriteOpOmapRmKeys(op, keys, keysLen);
}

void CephProxyWriteOpOmapClear(ceph_proxy_op_t op)
{
	RadosWriteOpOmapClear(op);
}

void CephProxyWriteOpSetAllocHint(ceph_proxy_op_t op, uint64_t expectedObjSize, uint64_t expectedWriteSize, uint32_t flags)
{
	RadosWriteOpSetAllocHint(op, expectedObjSize, expectedWriteSize, flags);
}

int CephProxyReadOpInit(ceph_proxy_op_t *op, const char *pool, const char* oid)
{
	*op = RadosReadOpInit(pool, oid);
	if (*op == nullptr) {
	std::cout << "Create ReadOp failed." << std::endl;
	return -1;
	}
	return 0;
}

int CephProxyReadOpInit2(ceph_proxy_op_t *op, const int64_t poolId, const char* oid)
{

	*op = RadosReadOpInit2(poolId, oid);
	if (*op == nullptr) {
	std::cout << "Create ReadOp failed." << std::endl;
	return -1;
	}
	return 0;
}

void CephProxyReadOpRelease(ceph_proxy_op_t op)
{
	RadosReadOpRelease(op);
}

void CephProxyReadOpSetFlags(ceph_proxy_op_t op, int flags)
{
	RadosReadOpSetFlags(op, flags);
}

void CephProxyReadOpAssertExists(ceph_proxy_op_t op)
{
	RadosReadOpAssertExists(op);
}

void CephProxyReadOpAssertVersion(ceph_proxy_op_t op, uint64_t ver)
{
	RadosReadOpAssertVersion(op, ver);
}

void CephProxyReadOpCmpext(ceph_proxy_op_t op, const char *cmpBuf, size_t cmpLen, uint64_t off, int *prval)
{
	RadosReadOpCmpext(op, cmpBuf, cmpLen, off, prval);
}

void CephProxyReadOpCmpXattr(ceph_proxy_op_t op,  const char *name, uint8_t compOperator, const char *value, size_t valLen)
{
	RadosReadOpCmpXattr(op, name, compOperator, value, valueLen);
}

void CephProxyReadOpGetXattrs(ceph_proxy_op_t op, proxy_xattrs_iter_t *iter, int *prval)
{
	RadosReadOpGetXattrs(op, iter, prval);
}

void CephProxyReadOpOmapCmp(ceph_proxy_op_t op, const char *key, uint8_t compOperator, const char *val, size_t valLen, int *prval)
{
	RadosReadOpOmapCmp(op, key, compOperator, val, valLen, prval);
}

void CephProxyReadOpStat(ceph_proxy_op_t op, uint64_t *psize, time_t *pmtime, int *prval)
{
	RadosReadOpStat(op, psize, pmtime, prval);
}

void CephProxyReadOpRead(ceph_proxy_op_t op, uint64_t offset, size_t len, char *buffer, size_t *bytesRead, int *prval)
{
	RadosReadOpRead(op, offset, len, buffer, bytesRead, prval);
}

void CephProxyReadOpReadSGL(ceph_proxy_op_t op, uint64_t offset, size_t len, SGL_S *s, int *prval)
{
	RadosReadOpReadSGL(op, offset, len , s, prval);
}

void CephProxyReadOpCheckSum(ceph_proxy_op_t op, proxy_checksum_type_t type, const char *initValue, size_t initValueLen, uint64_t offset, size_t len, size_t chunkSize, char *pCheckSum, size_t checkSumLen, int *prval) 
{
	RadosReadOpCheckSum(op ,type,  initValue, initValueLen, offset, len, chunkSize, pCheckSum, checkSumLen, prval);
}

completion_t CephProxyCreateCompletion(CallBack_t fn, void *arg)
{
	return CompletionInit(fn, arg);
}

void CephProxyCompletionDestroy(completion_t c) {
	CompletionDestroy(c);
}

