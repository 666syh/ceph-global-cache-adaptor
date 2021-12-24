#include "CephProxyInterface.h"
#include "RadosWrapper.h"
#include "CephProxyOp.h"
#include "assert.h"
#include "CephProxyLog.h"
#include "ConfigRead.h"

#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <cstddef>
#include <time.h>
#include <algorithm>
#include <string>
#include <list>
#include <map>
#include <set>

using namespace std;
using namespace librados;

#define DEFAULT_SGL_PAGE 4096
#define RADOS_CONNECT_RETRY 5
#define CONNECT_WAIT_TIME 5

#define MIN_ALLOC_SIZE_NAME "bluestore_min_alloc_size"
#define HDD_MIN_ALLOC_SIZE_NAME "bluestore_min_alloc_size_hdd"
#define SSD_MIN_ALLOC_SIZE_NAME "bluestore_min_alloc_size_ssd"
#define LOG_FILE_NAME        "log_file"

static void RadosBindCore(std::vector<uint32_t> coreId, uint64_t ii, uint32_t n)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    uint32_t cpuId = coreId[n % coreId.size()];
    ProxyDbgLogInfo("%lu cpuId = %u", ii, cpuId);

    CPU_SET(cpuId, &set);
    pid_t tid = ii;
    if (sched_setaffinity(tid, sizeof(set), &set) == -1) {
	ProxyDbgLogErr("setaffinity failed: %lu", ii);
	return;
    }

    CPU_ZERO(&set);
    if (sched_getaffinity(tid, sizeof(set), &set) == -1) {
	ProxyDbgLogErr("getaffinity failed: %lu", ii);
    }

    int cpus = sysconf(_SC_NPROCESSORS_CONF);
    for (int i = 0; i < cpus; i++) {
	if (CPU_ISSET(i, &set)) {
	     ProxyDbgLogInfo("this thread %lu running processor: %d", ii, i);
	}
    }
}

static int easy_readdir(const std::string& dir, std::set<std::string> *out)
{
    DIR *h = opendir(dir.c_str());
    if (!h) {
	return -errno;
    }

    struct dirent *de = nullptr;
    while ((de = readdir(h))) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
	    continue;
	}
	out->insert(de->d_name);
    }

    closedir(h);
    return 0;
}

void RadosBindMsgrWorker(std::vector<uint32_t> coreId, pid_t pid)
{
    uint32_t msgrNum = 0;
    std::set<std::string> ls;
    char path[128] = {0};
    sprintf(path, "/proc/%d/task", pid);
    int r = easy_readdir(path, &ls);
    if (r != 0) {
	 ProxyDbgLogErr("readiir (%s) failed: %d", path, r);
	 return;
    }

     ProxyDbgLogDebug("path = %s, ls_size = %lu", path, ls.size());
     vector<uint64_t> vecBindMsgr;
     vector<uint64_t> vecBindDispatch;

     for (auto &i : ls) {
	 string readPath = path;
	 readPath += "/" + i + "/status";
	 FILE *fp = fopen(readPath.c_str(), "r");
	 ProxyDbgLogDebug("readPath = %s", readPath.c_str());
	 if (NULL == fp) {
	     ProxyDbgLogErr("open file error: %s", readPath.c_str());
	     return;
	 }

	 if (!feof(fp)) {
	    char line[256] = {0};
	    memset(line, 0, sizeof(line));
	    char *result = fgets(line, sizeof(line) - 1, fp);
	    if (result == NULL) {
		ProxyDbgLogErr("readline %s failed: %d\n", readPath.c_str(), errno);
		return ;
	    }

	    string strLine = line;
	    if (strLine.find("msgr-worker-") != string::npos) {
		vecBindMsgr.push_back(atoll(i.c_str()));
		msgrNum++;
	    }

	    if (strLine.find("ms_dispatch") != string::npos) {
	        vecBindDispatch.push_back(atoll(i.c_str()));
	    }
	}
	ProxyDbgLogDebug("i = %s", i.c_str());
	fclose(fp);
    }

    sort(vecBindMsgr.rbegin(), vecBindMsgr.rend());
    sort(vecBindDispatch.rbegin(), vecBindDispatch.rend());

    uint32_t n = 0;
    ProxyDbgLogDebug("msgrNum = %d", msgrNum);
    for (auto &ii : vecBindMsgr) {
	RadosBindCore(coreId, ii, n);
	n++;
	if (n >= msgrNum) {
	    break;
	}
    }

    if (!vecBindDispatch.empty()) {
	RadosBindCore(coreId, vecBindDispatch[0], n);
    }
}
	

int RadosClientInit(rados_client_t *client,const std::string &cephConf)
{
	int ret =0;
	uint32_t retryCount = 0;
	librados::Rados *rados = new(std::nothrow) Rados();
	if (rados == nullptr) {
		ProxyDbgLogErr("Allocate Memory failed.");
		return -1;
	}

	std::string str = std::to_string(ProxyGetMonTimeOut());
	ret = rados->init("admin");
	if (ret < 0) {
		return ret;
	}

	ret = rados->conf_read_file(cephConf.c_str());
	if (ret < 0) {
		goto client_init_out;
	}

	ret = rados->conf_set(ProxyGetMonTimeOutOption(), str.c_str());
	if (ret != 0) {
		 ProxyDbgLogErr("set conf<%s, %s> failed: %d", ProxyGetMonTimeOutOption(), str.c_str(), ret);
		 goto client_init_out;
	}
        ProxyDbgLogInfo("set config<%s, %s> success.", ProxyGetMonTimeOutOption(), str.c_str());

	str = std::to_string(ProxyGetOsdTimeOut());
	ret = rados->conf_set(ProxyGetOsdTimeOutOption(), str.c_str());
	if (ret != 0) {
		 ProxyDbgLogErr("set conf<%s, %s> failed: %d", ProxyGetOsdTimeOutOption(), str.c_str(), ret);
		 goto client_init_out;
	}
        ProxyDbgLogInfo("set config<%s, %s> success.", ProxyGetOsdTimeOutOption(), str.c_str());

	ret = rados->conf_set(LOG_FILE_NAME, ProxyGetLogPath());
	if (ret != 0) {
		 ProxyDbgLogErr("set log<%s, %s> failed: %d", LOG_FILE_NAME, ProxyGetLogPath());
		 goto client_init_out;
	}
        ProxyDbgLogInfo("set config<%s, %s> success.", LOG_FILE_NAME, ProxyGetLogPath());

	while (retryCount < RADOS_CONNECT_RETRY) {
		ret = rados->connect();
		if ( ret < 0) {
		     ProxyDbgLogErr("connect ceph monitor failed: %d, retry:[%u/%d]", ret, retryCount + 1, RADOS_CONNECT_RETRY);
		     retryCount++;
		     sleep(CONNECT_WAIT_TIME * (1 << retryCount));
		     continue;
		} else {
			break;
		}
	}

	if (ret < 0) {
		ProxyDbgLogErr("connect ceph monitor failed: %d", ret);
	     	goto client_init_out;
	}

	*client = rados;
	return 0;

client_init_out:
	rados->shutdown();
	delete rados;
	*client = nullptr;
	return ret;
}


int RadosSetConf(rados_client_t client, const char *option, const char *value)
{
	int ret = 0;
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	ret = rados->conf_set(option, value);
	if (ret != 0) {
		 ProxyDbgLogErr("set conf<%s, %s> failed: %d", option, value, ret);
		 return ret;
	}
	ProxyDbgLogInfo("set config<%s, %s> success.", option, value);

	return 0;
}
	
int RadosCreateIoCtx(rados_client_t client, const std::string &poolname,rados_ioctx_t *ctx)
{
	int ret = 0;
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	librados::IoCtx *ioctx = new(std::nothrow) librados::IoCtx();
	if (ioctx == nullptr) {
		ProxyDbgLogErr("Allocate Memory Failed.");
		return -1;
	}

	ret = rados->ioctx_create(poolname.c_str(), *ioctx);
	if (ret < 0) {
		ProxyDbgLogErr("create ioctx failed: %d", ret);
		return ret;
	}

	*ctx = ioctx;
	return 0;
}

int RadosCreateIoCtx2(rados_client_t client, const int64_t poolId,rados_ioctx_t *ctx)
{
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);	
	librados::IoCtx *ioctx = new(std::nothrow) librados::IoCtx();
	if (ioctx == nullptr) {
		ProxyDbgLogErr("Allocate Memory failed.");
		return -1;
	}

	int ret = rados->ioctx_create2(poolId, *ioctx);	
	if ( ret < 0 ) {	
		ProxyDbgLogErr("create ioctx by poolId failed: %d", ret);
		return ret;
	}

	*ctx = ioctx;
	return 0;
}

void RadosReleaseIoCtx(rados_ioctx_t ctx)
{
	if (ctx != nullptr) {
		librados::IoCtx *ioctx = reinterpret_cast<librados::IoCtx *>(ctx);
		delete ioctx;
		ctx = nullptr;
	}
}

int64_t RadosGetPoolId(rados_ioctx_t ctx)
{
	librados::IoCtx *ioctx = reinterpret_cast<librados::IoCtx *>(ctx);
	return ioctx->get_id();
}

int RadosGetPoolName(rados_ioctx_t ctx, char *buf, unsigned maxLen)
{
	librados::IoCtx *ioctx = reinterpret_cast<librados::IoCtx *>(ctx);
	std::string poolName = ioctx->get_pool_name();
	if (poolName.length() >= maxLen) {
		return -ERANGE;
	}

	strcpy(buf, poolName.c_str());
	return 0;
}

void RadosClientShutdown(rados_client_t client)
{
	if(client != nullptr) {
		librados::Rados *rados = reinterpret_cast<Rados *>(client);
		rados->shutdown();
		delete rados;
		client = nullptr;
	}
}

int RadosGetMinAllocSizeHDD(rados_client_t client, uint32_t *minAllocSize)
{
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	std::string val;
	int ret = rados->conf_get(MIN_ALLOC_SIZE_NAME, val);
	if (ret < 0 ) {
		ProxyDbgLogErr("get cluster stat failed: %d.", ret);
		return ret;
	}

	sscanf(val.c_str(), "%u", minAllocSize);
	if (*minAllocSize == 0) {
		ret = rados->conf_get(HDD_MIN_ALLOC_SIZE_NAME, val);
		if (ret < 0 ) {
			ProxyDbgLogErr("get cluster stat failed: %d.", ret);
			return ret;
		}
		sscanf(val.c_str(), "%u", minAllocSize);
	}
	return 0;
}

int RadosGetMinAllocSizeSSD(rados_client_t client, uint32_t *minAllocSize)
{
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	std::string val;
	int ret = rados->conf_get(MIN_ALLOC_SIZE_NAME, val);
	if (ret < 0 ) {
		ProxyDbgLogErr("get cluster stat failed: %d.", ret);
		return ret;
	}

	sscanf(val.c_str(), "%u", minAllocSize);
	if (*minAllocSize == 0) {
		ret = rados->conf_get(SSD_MIN_ALLOC_SIZE_NAME, val);
		if (ret < 0 ) {
			ProxyDbgLogErr("get cluster stat failed: %d.", ret);
			return ret;
		}
		sscanf(val.c_str(), "%u", minAllocSize);
	}
	return 0;
}

int RadosGetClusterStat(rados_client_t client, CephClusterStat *stat)
{	
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	cluster_stat_t result;
	int ret = rados->cluster_stat(result);
	if (ret < 0) {
		ProxyDbgLogErr("get cluster stat failed: %d", ret);
		return ret;
	}

	stat->kb = result.kb;
	stat->kbAvail = result.kb_avail;
	stat->kbUsed = result.kb_used;
	stat->numObjects = result.num_objects;
	return 0;
}

int RadosGetPoolStat(rados_client_t client, rados_ioctx_t ctx, CephPoolStat *stat)
{
	librados::IoCtx *ioctx = reinterpret_cast<librados::IoCtx *>(ctx);
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);

	std::string pool_name = ioctx->get_pool_name();
	std::list<std::string> ls;
	ls.push_back(pool_name);

	std::map<std::string,pool_stat_t> rawresult;
	int ret =rados->get_pool_stats(ls,rawresult);
	if (ret !=0) {
		ProxyDbgLogErr("get pool stat failed: %d", ret);;
		return ret;
	}

	pool_stat_t &stats = rawresult[pool_name];
		
	stat->numKb = stats.num_kb;
	stat->numBytes = stats.num_bytes;
	stat->numObjects = stats.num_objects;
	stat->numObjectClones = stats.num_object_clones;
	stat->numObjectCopies = stats.num_object_copies;
	stat->numObjectsMissingOnPrimary = stats.num_objects_missing_on_primary;
	stat->numObjectsUnfound = stats.num_objects_unfound;
	stat->numObjectsDegraded = stats.num_objects_degraded;
	stat->numRd = stats.num_rd;
	stat->numRdKb = stats.num_rd_kb;
	stat->numWr = stats.num_wr;
	stat->numWrKb = stats.num_wr_kb;
	stat->numUserBytes = stats.num_user_bytes;
	stat->compressedBytesOrig = stats.compressed_bytes_orig;
	stat->compressedBytes = stats.compressed_bytes;
	stat->compressedBytesAlloc = stats.compressed_bytes_alloc;

	return 0;
}


rados_op_t RadosWriteOpInit(const string& pool, const string &oid)
{
	RadosObjectWriteOp *writeOp = new(std::nothrow)  RadosObjectWriteOp(pool,oid);	
	if (writeOp == nullptr) {
		ProxyDbgLogErr("Allocate WriteOp Failed.");
		return nullptr;
	}
	rados_op_t op = reinterpret_cast<void *>(writeOp);

	return op;
}	

rados_op_t RadosWriteOpInit2(const int64_t poolId, const string &oid)
{
	RadosObjectOperation *writeOp = new(std::nothrow) RadosObjectWriteOp(poolId,oid);	
	if (writeOp == nullptr) {
		ProxyDbgLogErr("Allocate WriteOp Failed.");
		return nullptr;
	}
	rados_op_t op = reinterpret_cast<void *>(writeOp);
	return op;
}

void RadosWriteOpRelease(rados_op_t op)
{
	if (op != nullptr) {
		RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
		delete writeOp;
		op = nullptr;
	}
}

void RadosWriteOpSetFlags(rados_op_t op, int flags)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.set_op_flags2(flags);
}

void RadosWriteOpAssertExists(rados_op_t op)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.assert_exists();
}

void RadosWriteOpAssertVersion(rados_op_t op, uint64_t ver)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.assert_version(ver);
}

void RadosWriteOpCmpext(rados_op_t op, const char *cmpBuf,
			size_t cmpLen, uint64_t off, int *prval)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	bufferlist cmpBl;
	cmpBl.append(cmpBuf, cmpLen);
	writeOp->op.cmpext(off, cmpBl, prval);
}

void RadosWriteOpCmpXattr(rados_op_t op, const char *name,
			uint8_t compOperator, const char *value, size_t valLen)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	bufferlist valueBl;
	valueBl.append(value, valLen);
	writeOp->op.cmpxattr(name, compOperator, valueBl);
}

void RadosWriteOpOmapCmp(rados_op_t op, const char *key, uint8_t compOperator,
			const char *value, size_t valLen, int *prval)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	bufferlist bl;
	bl.append(value, valLen);
	std::map<std::string,pair<bufferlist, int>> assertions;
	std::string lkey = string(key, strlen(key));
	writeOp->op.omap_cmp(assertions, prval);
}

void RadosWriteOpSetXattr(rados_op_t op, const char *name, const char *value, size_t valLen)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	bufferlist bl;
	bl.append(value, valLen);
	writeOp->op.setxattr(name, bl);
}

void RadosWriteOpRemoveXattr(rados_op_t op, const char *name)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.rmxattr(name);
}

void RadosWriteOpCreateObject(rados_op_t op, int exclusive, const char *category)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.create(!!exclusive);
}

void RadosWriteOpWrite(rados_op_t op, const char *buffer, size_t len, uint64_t off)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->bl.append(buffer, len);	
	writeOp->op.write(off, writeOp->bl);
}

void RadosWriteOpWriteSGL(rados_op_t op, SGL_S *sgl, size_t len1, uint64_t off, char *buffer, size_t len2, int isRelease)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	uint32_t leftLen = len1;
	uint32_t curSrcEntryIndex = 0;
	while(leftLen > 0){
		size_t size = 0;
		if (isRelease) {
			size = std::min((uint32_t)DEFAULT_SGL_PAGE, leftLen);
		} else {
			size = std::min(sgl->entrys[curSrcEntryIndex].len, leftLen);
		}
		writeOp->bl.append(sgl->entrys[curSrcEntryIndex].buf, size);
		leftLen -= size;
		curSrcEntryIndex++;
		if (curSrcEntryIndex >= sgl->entrySumInSgl) {
			curSrcEntryIndex = 0;
			sgl = sgl->nextSgl;
		}
	}
	
	if (buffer != NULL && len2 != 0) {
		writeOp->bl.append(buffer, len2);
	}

	writeOp->op.write(off, writeOp->bl);
}	

void RadosWriteOpWriteFull(rados_op_t op, const char *buffer, size_t len)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->bl.append(buffer, len);						
	writeOp->op.write_full(writeOp->bl);
}

void RadosWriteOpWriteFullSGL(rados_op_t op, const SGL_S *sgl, size_t len, int isRelease)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	uint32_t leftLen = len;
	uint32_t curSrcEtnryIndex = 0;
	while (leftLen > 0) {
		size_t size =0;
		if (isRelease) {
			size = std::min((uint32_t)DEFAULT_SGL_PAGE, leftLen);
		} else {
			size = std::min(sgl->entrys[curSrcEtnryIndex].len, leftLen);
		}

		writeOp->bl.append(sgl->entrys[curSrcEtnryIndex].buf, size);
		leftLen -= size;
		curSrcEtnryIndex++;
		if (curSrcEtnryIndex >= sgl->entrySumInSgl) {
			curSrcEtnryIndex = 0;
			sgl = sgl->nextSgl;
		}
	}

	writeOp->op.write_full(writeOp->bl);
}	

void RadosWriteOpWriteSame(rados_op_t op, const char *buffer,
			size_t dataLen, size_t writeLen, uint64_t off)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->bl.append(buffer, dataLen);
	writeOp->op.writesame(off,writeLen, writeOp->bl);	
}

void RadosWriteOpWriteSameSGL(rados_op_t op, const SGL_S *s, size_t dataLen,
			size_t writeLen, uint64_t off, int isRelease)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	uint32_t leftLen = dataLen;
	uint32_t curSrcEntryIndex = 0;
	while (leftLen > 0) {
		size_t size =0;
		if (isRelease) {
			size = std::min((uint32_t)DEFAULT_SGL_PAGE, leftLen);
		} else {
			size = std::min(s->entrys[curSrcEntryIndex].len, leftLen);
		}
		writeOp->bl.append(s->entrys[curSrcEntryIndex].buf, size);
		leftLen -= size;
		curSrcEntryIndex++;
		if (curSrcEntryIndex >= s->entrySumInSgl) {
			curSrcEntryIndex = 0;
			s = s->nextSgl;		
		}
    	}

	writeOp->op.writesame(off,writeLen,  writeOp->bl);
}

void RadosWriteOpAppend(rados_op_t op, const char *buffer, size_t len)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	bufferlist bl;	
	bl.append(buffer,len);
	writeOp->op.append(bl);
}

void RadosWriteOpAppendSGL(rados_op_t op, const SGL_S *s, size_t len, int isRelease)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	uint32_t leftLen = len;
	uint32_t curSrcEntryIndex = 0;
	while (leftLen > 0) {
		size_t size =0;
		if (isRelease) {
			size = std::min((uint32_t)DEFAULT_SGL_PAGE, leftLen);
		} else {
			size = std::min(s->entrys[curSrcEntryIndex].len, leftLen);
		}

		writeOp->bl.append(s->entrys[curSrcEntryIndex].buf, size);
		leftLen -= size;
		curSrcEntryIndex++;
		if (curSrcEntryIndex >= s->entrySumInSgl) {
			curSrcEntryIndex = 0;
			s = s->nextSgl;
		}	
	}

	writeOp->op.append(writeOp->bl);
}

void RadosWriteOpRemove(rados_op_t op)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.remove();
}

void RadosWriteOpTruncate(rados_op_t op, uint64_t off)	
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.truncate(off);
}

void RadosWriteOpZero(rados_op_t op, uint64_t off, uint64_t len)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.zero(off, len);
}

void RadosWriteOpOmapSet(rados_op_t op, const char *const *keys,
		const char *const *vals, const size_t *lens, size_t num)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	std::map<std::string, bufferlist> entries;
	for (size_t i=0; i < num; i++) {
		bufferlist bl(lens[i]);
		bl.append(vals[i],lens[i]);	
		entries[keys[i]] = bl;
	}
	
	writeOp->op.omap_set(entries);	
}

void RadosWriteOpOmapRmKeys(rados_op_t op, const char *const *keys, size_t keysLen)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	std::set<std::string> to_remove(keys, keys + keysLen);	
	writeOp->op.omap_rm_keys(to_remove);
}

void RadosWriteOpOmapClear(rados_op_t op)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.omap_clear();
}

void RadosWriteOpSetAllocHint(rados_op_t op, uint64_t expectedObjSize, uint64_t expectedWriteSize, uint32_t flags)
{
	RadosObjectWriteOp *writeOp = reinterpret_cast<RadosObjectWriteOp *>(op);
	writeOp->op.set_alloc_hint2(expectedObjSize, expectedWriteSize, flags);
}


rados_op_t RadosReadOpInit(const string& pool, const string &oid)
{
	RadosObjectReadOp *readOp = new(std::nothrow) RadosObjectReadOp(pool, oid);
	if (readOp == nullptr) {
		ProxyDbgLogErr("Allocate ReadOp Failed.");
		return nullptr;
	}
	rados_op_t op = reinterpret_cast<void *>(readOp);
	return op;
}

rados_op_t RadosReadOpInit2(const int64_t poolId,const string &oid)
{
	RadosObjectReadOp *readOp = new(std::nothrow) RadosObjectReadOp(poolId, oid);
	if (readOp == nullptr) {
		ProxyDbgLogErr("Allocate ReadOp Failed.");
		return nullptr;
	}
	rados_op_t op = reinterpret_cast<void *>(readOp);
	return op;
}

void RadosReadOpRelease(rados_op_t op)
{
	if (op != nullptr) {
		RadosObjectReadOp *readOp= reinterpret_cast<RadosObjectReadOp *>(op);
		delete readOp;
		op = nullptr;
	}
}

void RadosReadOpSetFlags(rados_op_t op, int flags)	
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	readOp->op.set_op_flags2(flags);
}

void RadosReadOpAssertExists(rados_op_t op)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	readOp->op.assert_exists();
}

void RadosReadOpAssertVersion(rados_op_t op, uint64_t ver)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	readOp->op.assert_version(ver);
}

void RadosReadOpCmpext(rados_op_t op, const char *cmpBuf,
			size_t cmpLen, uint64_t off, int *prval)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	bufferlist bl;
	bl.append(cmpBuf, cmpLen);
	readOp->op.cmpext(off, bl, prval);
}

void RadosReadOpCmpXattr(rados_op_t op, const char *name, uint8_t compOperator,
			const char *value, size_t valueLen)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	bufferlist bl;
	bl.append(value, valueLen);
	readOp->op.cmpxattr(name, compOperator, bl);
}

void RadosReadOpGetXattr(rados_op_t op, const char *name, char **val, int *prval)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	readOp->reqCtx.xattr.vals = val;
	readOp->reqCtx.xattr.name = name;
	string key(name);
	readOp->op.getxattr(name, &(readOp->xattrs[name]), prval);
}

void RadosReadOpGetXattrs(rados_op_t op, proxy_xattrs_iter_t *iter, int *prval)
{
	RadosObjectReadOp *readOp=reinterpret_cast<RadosObjectReadOp *>(op);
	RadosXattrsIter *xIter = new(std::nothrow) RadosXattrsIter();
	if (xIter == nullptr) {
		ProxyDbgLogErr("Aclloate xIter Failed.");
		return;
	}
	readOp->op.getxattrs(&(xIter->attrset), prval);
	readOp->reqCtx.xattrs.iter = xIter;
	*iter = xIter;
}

int RadosGetXattrsNext(proxy_xattrs_iter_t iter, const char **name, const char **val, size_t *len)
{
	RadosXattrsIter *it = static_cast<RadosXattrsIter*>(iter);
	if (it->val) {
		free(it->val);
		it->val = nullptr;
	}
	
	if (it->i == it->attrset.end()){
		*name = nullptr;
		*val = nullptr;
		*len = 0;
		return 0;
	}

	const std::string &s(it->i->first);
	*name = s.c_str();
	bufferlist &bl(it->i->second);
	size_t blLen = bl.length();
	if (!blLen) {
		*val = (char *)NULL;
	}else{
		it->val = (char *)malloc(blLen);
		if (!it->val) {
			return -ENOMEM;
		}
		memcpy(it->val, bl.c_str(), blLen);
		*val = it->val;
	}
	*len = blLen;
	++it->i;
	return 0;	
}

void RadosGetXattrsEnd(proxy_xattrs_iter_t iter)
{
	RadosXattrsIter *it = static_cast<RadosXattrsIter *>(iter);
	delete it;
}

void RadosReadOpOmapGetVals(rados_op_t op, const char *startAfter,
	uint64_t maxReturn, rados_omap_iter_t *iter,
	unsigned char *pmore, int *prval)
{
	RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
	RadosOmapIter *oIter = new(std::nothrow) RadosOmapIter();
	if (oIter == nullptr) {
		ProxyDbgLogErr("Allocate oIter failed.");
		return;
	}
	const char *start = startAfter ? startAfter : "";
	readOp->reqCtx.omap.iter = oIter;
	readOp->op.omap_get_vals2(start, maxReturn, &(oIter->values), (bool *)pmore, prval);
	*iter = oIter;
}

void RadosReadOpOmapGetKeys(rados_op_t op, const char *startAfter, uint64_t maxReturn,
				proxy_omap_iter_t *iter, unsigned char *pmore, int *prval)
{
	RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
	RadosOmapIter *oIter = new(std::nothrow) RadosOmapIter();
	if (oIter == nullptr) {
		ProxyDbgLogErr("Allocate oIter failed.");
		return;
	}
	const char *start = startAfter ? startAfter : "";
	readOp->reqCtx.omap.iter = oIter;
	readOp->op.omap_get_keys2(start, maxReturn, &(oIter->keys), (bool *)pmore, prval);
	*iter = oIter;
}

int RadosOmapGetNext(proxy_omap_iter_t iter, char **key, char **val, size_t *keyLen, size_t *valLen)
{
    RadosOmapIter *it = static_cast<RadosOmapIter *>(iter);
    if (it->i == it->values.end()) {
	if (key) {
	   *key = nullptr;
	}

	if (val) {
	   *val = nullptr;
	}

	if (keyLen) {
	   *keyLen = 0;
	}

	if (valLen) {
	   *valLen = 0;
	}

        return 0 ;
    }

    if (key) {
	*key = (char *)it->i->first.c_str();
    }

    if (val) {
	*val = (char *)it->i->second.c_str();
    }

    if (keyLen) {
	*keyLen = it->i->first.length();
    }

    if (valLen) {
	*valLen = it->i->second.length();
    }

    ++it->i;
    return 0;
}

size_t RadosOmapIterSize(proxy_omap_iter_t iter) {
    RadosOmapIter *it = static_cast<RadosOmapIter *>(iter);
    return it->values.size();
}

void RadosOmapIterEnd(proxy_omap_iter_t iter)
{
    RadosOmapIter *it = static_cast<RadosOmapIter *>(iter);
    delete it;
}

void RadosReadOpOmapCmp(rados_op_t op, const char *key, uint8_t compOperator,
			const char *val, size_t valLen, int *prval)
{
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    bufferlist bl;
    bl.append(val, valLen);
    std::map<std::string, pair<bufferlist, int>> assertions;
    string lkey = string(key,strlen(key));

    assertions[lkey] = std::make_pair(bl, compOperator);
    readOp->op.omap_cmp(assertions, prval);
}

void RadosReadOpStat(rados_op_t op, uint64_t *psize, time_t *pmtime, int *prval)
{
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    readOp->op.stat(psize, pmtime, prval);
}

void RadosReadOpRead(rados_op_t op, uint64_t offset, size_t len, char *buffer,
			size_t *bytesRead, int *prval)
{
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    readOp->reqCtx.read.buffer = buffer;
    readOp->reqCtx.read.bytesRead = bytesRead;

    readOp->op.read(offset, len, &(readOp->results), prval);
}

void RadosReadOpReadSGL(rados_op_t op, uint64_t offset,size_t len, SGL_S *sgl, int *prval, int isRelease)
{
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    readOp->reqCtx.readSgl.sgl = sgl;
    readOp->reqCtx.readSgl.len = len;
    readOp->reqCtx.readSgl.buildType = isRelease;

    readOp->op.read(offset, len, &(readOp->results), prval);
}

void RadosReadOpCheckSum(rados_op_t op, proxy_checksum_type_t type,
			const char *initValue, size_t initValueLen,
			uint64_t offset, size_t len, size_t chunkSize,
			char *pCheckSum, size_t checkSumLen, int *prval)
{
    rados_checksum_type_t  rtype = (rados_checksum_type_t)type;
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    bufferlist bl;
    bl.append(initValue, initValueLen);
    readOp->reqCtx.checksum.pCheckSum = pCheckSum;
    readOp->reqCtx.checksum.chunkSumLen = checkSumLen;
    readOp->op.checksum(rtype, bl, offset, len, chunkSize, &(readOp->checksums), prval);
}

void RadosReadOpExec(rados_op_t op, const char *cls, const char *method,
			const char *inBuf, size_t inLen, char **outBuf,
			size_t *outLen, int *prval)
{
    RadosObjectReadOp *readOp = reinterpret_cast<RadosObjectReadOp *>(op);
    bufferlist inbl;
    inbl.append(inBuf, inLen);

    readOp->reqCtx.exec.outBuf = outBuf;
    readOp->reqCtx.exec.outLen = outLen;
    readOp->op.exec(cls, method, inbl, &(readOp->execOut),prval);
}

int RadosOperationOperate(rados_op_t op, rados_ioctx_t io)
{
    RadosObjectOperation *rop = reinterpret_cast<RadosObjectOperation *>(op);
    librados::IoCtx *ctx =  reinterpret_cast<librados::IoCtx *>(io);
    int ret = 0;
    switch(rop->opType) {
	case BATCH_READ_OP: {
	        RadosObjectReadOp *readOp = dynamic_cast<RadosObjectReadOp *>(rop);
		bufferlist bl;
        	ret = ctx->operate(readOp->objectId, &(readOp->op), &bl);
	}
        break;
	case BATCH_WRITE_OP: {
	        RadosObjectWriteOp *writeOp = dynamic_cast<RadosObjectWriteOp *>(rop);
        	ret = ctx->operate(writeOp->objectId, &(writeOp->op));
        }
	break;
	default:
	break;
    }
    
    return ret;
}

void ReadCallback(rados_completion_t c, void *arg)
{
    RadosObjectReadOp *readOp = (RadosObjectReadOp *)arg;
    int ret = rados_aio_get_return_value(c);
    if (ret == 0 && readOp->results.length() > 0) {
	if (readOp->reqCtx.read.buffer != nullptr) {   

	    memcpy(readOp->reqCtx.read.buffer, readOp->results.c_str(), readOp->results.length());
	} else if (readOp->reqCtx.readSgl.sgl != nullptr) {
	    size_t len = readOp->results.length();
	    uint32_t leftLen = len;
	    int curEntryIndex = 0;
	    uint64_t offset = 0;
	    SGL_S *sgl = readOp->reqCtx.readSgl.sgl;
	    int buildType = readOp->reqCtx.readSgl.buildType;
  
 	    while (leftLen > 0) {
		size_t size = 0;
		if (buildType) {
			size = std::min((uint32_t)DEFAULT_SGL_PAGE, leftLen);
		} else {
			size = std::min(sgl->entrys[curEntryIndex].len, leftLen);
		}

		bufferlist bl;
		bl.substr_of(readOp->results, offset, size);
		memcpy(sgl->entrys[curEntryIndex].buf, bl.c_str(), size);
		leftLen -= size;
		curEntryIndex++;
		if (curEntryIndex >= ENTRY_PER_SGL) {
		    curEntryIndex = 0;
		    sgl = sgl->nextSgl;
		}

		offset += size;
	    }
	}
    }
    
    if (ret == 0 && readOp->reqCtx.xattr.name != nullptr) {
	memcpy(*(readOp->reqCtx.xattr.vals),
		readOp->xattrs[readOp->reqCtx.xattr.name].c_str(),
		readOp->xattrs[readOp->reqCtx.xattr.name].length());
    }
   
    if (ret == 0 && readOp->reqCtx.xattrs.iter != nullptr) {
	RadosXattrsIter *iter = static_cast<RadosXattrsIter *>(readOp->reqCtx.xattrs.iter);
	iter->i = iter->attrset.begin();
    }

    if (ret == 0 && readOp->reqCtx.omap.iter != nullptr) {
	RadosOmapIter *iter = static_cast<RadosOmapIter *>(readOp->reqCtx.omap.iter);
	iter->i = iter->values.begin();
	if (!iter->keys.empty()) {
	    for (auto i : iter->keys) {
		iter->values[i];
	    }
        }
    }

    if (ret == 0 && readOp->reqCtx.checksum.pCheckSum != nullptr) {
	memcpy(readOp->reqCtx.checksum.pCheckSum,
		readOp->checksums.c_str(),
		readOp->reqCtx.checksum.chunkSumLen);
    }
    

    readOp->callback(ret, readOp->cbArg);
}

void WriteCallback(rados_completion_t c, void *arg)
{
	RadosObjectWriteOp *writeOp = (RadosObjectWriteOp *)arg;

	int ret = rados_aio_get_return_value(c);
	writeOp->callback(ret, writeOp->cbArg);
}

int RadosOperationAioOperate( rados_client_t client, rados_op_t op, rados_ioctx_t io, userCallback_t fn, void *cbArg)
{
	int ret = 0;
	librados::Rados *rados = reinterpret_cast<librados::Rados *>(client);
	RadosObjectOperation *rop = reinterpret_cast<RadosObjectOperation *>(op);
	librados::IoCtx *ctx = reinterpret_cast<librados::IoCtx*>(io);
	switch (rop->opType) {
	    case BATCH_READ_OP: {
	        RadosObjectReadOp *readOp = dynamic_cast< RadosObjectReadOp *>(rop);
		readOp->ts = 0;
	        readOp->callback = fn;
	        readOp->cbArg = cbArg;

	        librados::AioCompletion *readCompletion = rados->aio_create_completion(readOp, ReadCallback, NULL);
	        ret = ctx->aio_operate(readOp->objectId, readCompletion, &(readOp->op), NULL);
	        if (ret !=0) {
	            ProxyDbgLogErr("aio_operate failed: %d", ret);
		}
	        readCompletion->release();
	    }
	    break;
	    case BATCH_WRITE_OP: {
	        RadosObjectWriteOp *writeOp = dynamic_cast<RadosObjectWriteOp *>(rop);
		writeOp->ts = 0;
	        writeOp->callback = fn;
	        writeOp->cbArg = cbArg;
	        librados::AioCompletion *writeCompletion = rados->aio_create_completion(writeOp, WriteCallback, NULL);

	        ret=ctx->aio_operate(writeOp->objectId, writeCompletion, &(writeOp->op));
	        if (ret !=0) {
	            ProxyDbgLogErr("aio_operate failed: %d", ret);
		}
	        writeCompletion->release();
	    }
	    break;
	    default:
	    break;
	}
	return ret;
}
	
#ifdef __cplusplus
extern "C" {
#endif

bool ceph_status()
{
	int ret = 0;

	rados_t cluster;
	rados_create(&cluster, "admin");

	ret = rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
	if (ret < 0) {
		syslog(LOG_ERR, "Failed to read the ceph configuration file.\n");
		return false;
	}

	rados_conf_set(cluster, "client_mount_timeout", "0.9");
	rados_conf_set(cluster, "rados_mon_op_timeout", "0.9");

	ret = rados_connect(cluster);
	if (ret < 0) {
		syslog(LOG_ERR, "Failed to connect to the Rados.\n");
		return false;
	}

	struct rados_cluster_stat_t result;
	ret = rados_cluster_stat(cluster, &result);
	rados_shutdown(cluster);

	if (ret == 0) {
		syslog(LOG_INFO, "Connecting to the ceph cluster succeeded.\n");
		return true;
	} else {
		syslog(LOG_ERR, "Failed to connect to the ceph cluster.\n");
		return false;
	}
}

#ifdef __cplusplus
}
#endif

