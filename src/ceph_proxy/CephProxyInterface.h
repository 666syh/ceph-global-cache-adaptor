#ifndef _CEPH_PROXY_INTERFACE_H_
#define _CEPH_PROXY_INTERFACE_H_

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "sgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CEPHPROXY_CREATE_EXCLUSIVE 1
#define CEPHPROXY_CREATE_IDEMPOTENT 0

#define POOL_NAME_MAX_LEN 128
#define OBJECT_ID_MAX_LEN 128
#define CONFIG_PATH_LEN   128

enum {
    CEPHPROXY_OP_FLAG_EXCL                = 0x1,
    CEPHPROXY_OP_FLAG_FAILOK              = 0x2,
    CEPHPROXY_OP_FLAG_FADVISE_RANDOM      = 0x4,
    CEPHPROXY_FLAG_FADVISE_SEQUENTIAL     = 0x8,
    CEPHPROXY_OP_FLAG_FADVISE_WILLNEED    = 0x10,
    CEPHPROXY_OP_FLAG_FADVISE_DONTNEED    = 0x20,
    CEPHPROXY_OP_FLAG_FADVISE_NOCACHE     = 0x40,
    CEPHPROXY_OP_FLAG_FADVISE_FUA         = 0x80,
};

enum {
    CEPHPROXY_CMPXATTR_OP_EQ              = 1,
    CEPHPROXY_CMPXATTR_OP_NE              = 2,
    CEPHPROXY_CMPXATTR_OP_GT              = 3,
    CEPHPROXY_CMPXATTR_OP_GTE             = 4,
    CEPHPROXY_CMPXATTR_OP_LT              = 5,
    CEPHPROXY_CMPXATTR_OP_LTE             = 6
};

enum {
    CEPHPROXY_OPERATION_NOFLAG                   = 0, 
    CEPHPROXY_OPERATION_BALANCE_READS            = 1, 
    CEPHPROXY_OPERATION_LOCALIZE_READS           = 2, 
    CEPHPROXY_OPERATION_ORDER_READS_WRITES       = 4, 
    CEPHPROXY_OPERATION_IGNORE_CACHE             = 8, 
    CEPHPROXY_OPERATION_SKIPRWLOCKS              = 16,
    CEPHPROXY_OPERATION_IGNORE_OVERLAY           = 32,
    CEPHPROXY_OPERATION_FULL_TRY                 = 64, 
    CEPHPROXY_OPERATION_FULL_FORCE               = 128, 
    CEPHPROXY_OPERATION_IGNORE_REDIRECT          = 256, 
    CEPHPROXY_OPERATION_ORDERSNAP                = 512, 
};

enum {
    CEPHPROXY_ALLOC_HINT_FLAG_SEQUENTIAL_WRITE    = 1,
    CEPHPROXY_ALLOC_HINT_FLAG_RANDOM_WRITE        = 2,
    CEPHPROXY_ALLOC_HINT_FLAG_SEQUENTIAL_READ     = 4,
    CEPHPROXY_ALLOC_HINT_FLAG_RANDOM_READ         = 8,
    CEPHPROXY_ALLOC_HINT_FLAG_APPEND_ONLY         = 16,
    CEPHPROXY_ALLOC_HINT_FLAG_IMMUTABLE           = 32,
    CEPHPROXY_ALLOC_HINT_FLAG_SHORTLIVED          = 64,
    CEPHPROXY_ALLOC_HINT_FLAG_LONGLIVED           = 128,
    CEPHPROXY_ALLOC_HINT_FLAG_COMPRESSIBLE        = 256,
    CEPHPROXY_ALLOC_HINT_FLAG_INCOMPRESSIBLE      = 512,
};

typedef enum {
    SINGLE_OP      = 0x01,
    BATCH_READ_OP  = 0x02,
    BATCH_WRITE_OP = 0x03,
    MOSD_OP        = 0x04,
} CephProxyOpType;
 
typedef enum {
    PROXY_NOP            =0x01,
    PROXY_ASYNC_READ     =0x02,
    PROXY_READ           =0x03,
    PROXY_ASYNC_WRITE    =0x04,
    PROXY_WRITE          =0x05,
} CephProxyOpCode;

#define CLIENT_OPERATE_SUCCESS  0x00
#define CLIENT_INIT_ERR         0x01
#define CLIENT_READ_CONF_ERR    0x02
#define CLIENT_CONNECT_ERR      0x03
#define CLIENT_CREATE_IOCTX_ERR 0x04
#define CLIENT_LOG_INIT_ERR     0x05

#define ASYNC_NOP_ERR           0x01
#define ASYNC_READ_ERR          0x02
#define ASYNC_WRITE_ERR         0x03
#define ASYNC_OPERATE_ERR       0x04
#define ASYNC_READ_OPERATE_ERR  0x05
#define ASYNC_WRITE_OPERATE_ERR 0x06

#define POOL_HANDLER_TABLE_SUCCESS           0x00
#define POOL_HANDLER_TABLE_EXIST             0x01

#define PROXYOP_SUCCESS                0x00
#define PROXYOP_CREATE_ERR             0x01
#define PROXYOP_INVALID                0x02

typedef enum {
    BUFFER_INC = 0x01,
    DIRECT_INC = 0x02,
} CephProxyOpFrom;

typedef enum {
    PROXY_CHECKSUM_TYPE_XXHASH32 = 0x00,
    PROXY_CHECKSUM_TYPE_XXHASH64 = 0x01,
    PROXY_CHECKSUM_TYPE_CRC32C   = 0x02,
} proxy_checksum_type_t;

typedef struct {
    uint64_t num_bytes;
    uint64_t num_kb;
    uint64_t num_objects;
    uint64_t num_object_clones;
    uint64_t num_object_copies;
    uint64_t num_objects_missing_on_primary;
    uint64_t num_objects_unfound;
    uint64_t num_objects_degraded;
    uint64_t num_rd;
    uint64_t num_rd_kb;
    uint64_t num_wr;
    uint64_t num_wr_kb;
    uint64_t num_user_bytes;
    uint64_t compressed_bytes_orig;
    uint64_t compressed_bytes;
    uint64_t compressed_bytes_alloc;
} CephPoolStat;

typedef struct {
    uint64_t kb;
    uint64_t kb_used;
    uint64_t kb_avail;
    uint64_t num_objects;
} CephClusterStat;

typedef void *completion_t;
typedef void (*CallBack_t)(int ret, void *arg);
typedef void *ceph_proxy_op_t;
typedef void *rados_ioctx_t;
typedef void *ceph_proxy_t;
typedef uint64_t snap_t;
typedef void *proxy_xattrs_iter_t;
typedef void *proxy_omap_iter_t;
typedef void *rados_client_t;

int CephProxyInit(const char *conf, size_t wNum, const char *log, ceph_proxy_t *proxy);

void CephProxyShutdown(ceph_proxy_t proxy);

rados_ioctx_t CephProxyGetIoCtx(ceph_proxy_t proxy, const char *poolname);
 
rados_ioctx_t CephProxyGetIoCtx2(ceph_proxy_t proxy, const int64_t poolId);

int CephProxyGetClusterStat(ceph_proxy_t proxy, CephClusterStat *result);

int CephProxyGetPoolStat(ceph_proxy_t proxy, rados_ioctx_t ioctx, CephPoolStat *stats);

void CephProxyQueueOp(ceph_proxy_t proxy,rados_ioctx_t ioctx, ceph_proxy_op_t op, completion_t c);

int CephProxyWriteOpInit(ceph_proxy_op_t *op, const char *pool, const char* oid);

int CephProxyWriteOpInit2(ceph_proxy_op_t *op, const int64_t poolId, const char* oid);

void CephProxyWriteOpRelease(ceph_proxy_op_t op);

void CephProxyWriteOpSetFlags(ceph_proxy_op_t op, int flags);

void CephProxyWriteOpAssertExists(ceph_proxy_op_t op);

void CephProxyWriteOpAssertVersion(ceph_proxy_op_t op, uint64_t ver);

void CephProxyWriteOpCmpext(ceph_proxy_op_t op, const char *cmpBuf, size_t cmpLen, uint64_t off, int *prval);

void CephProxyWriteOpCmpXattr(ceph_proxy_op_t op,  const char *name, uint8_t compOperator, const char *value, size_t valLen);

void CephProxyWriteOpOmapCmp(ceph_proxy_op_t op, const char *key, uint8_t compOperator, const char *value, size_t valLen, int *prval);

void CephProxyWriteOpSetXattr(ceph_proxy_op_t op, const char *name, const char *value, size_t valLen);

void CephProxyWriteOpRemoveXattr(ceph_proxy_op_t op, const char *name);

void CephProxyWriteOpCreateObject(ceph_proxy_op_t op, int exclusive, const char *category);

void CephProxyWriteOpWrite(ceph_proxy_op_t op, const char *buffer, size_t len, uint64_t off);

void CephProxyWriteOpWriteSGL(ceph_proxy_op_t op, SGL_S *sgl, size_t len, uint64_t off);

void CephProxyWriteOpWriteFull(ceph_proxy_op_t op, const char *buffer, size_t len);

void CephProxyWriteOpWriteFullSGL(ceph_proxy_op_t op, const SGL_S *sgl, size_t len);

void CephProxyWriteOpWriteSame(ceph_proxy_op_t op, const char *buffer, size_t dataLen, size_t writeLen, uint64_t off);

void CephProxyWriteOpWriteSameSGL(ceph_proxy_op_t op, const SGL_S *sgl, size_t dataLen, size_t writeLen, uint64_t off);

void CephProxyWriteOpAppend(ceph_proxy_op_t op, const char *buffer, size_t len);

void CephProxyWriteOpAppendSGL(ceph_proxy_op_t op, const SGL_S *sgl, size_t len);

void CephProxyWriteOpRemove(ceph_proxy_op_t op);

void CephProxyWriteOpTruncate(ceph_proxy_op_t op,  uint64_t off);

void CephProxyWriteOpZero(ceph_proxy_op_t op,  uint64_t off, uint64_t len);

void CephProxyWriteOpOmapSet(ceph_proxy_op_t op, char const* const* keys, char const* const* vals, const size_t *lens, size_t num);

void CephProxyWriteOpOmapRmKeys(ceph_proxy_op_t op, char const* const* keys, size_t keysLen);

void CephProxyWriteOpOmapClear(ceph_proxy_op_t op);

void CephProxyWriteOpSetAllocHint(ceph_proxy_op_t op, uint64_t expectedObjSize, uint64_t expectedWriteSize, uint32_t flags);

int CephProxyReadOpInit(ceph_proxy_op_t *op, const char *pool, const char* oid);

int CephProxyReadOpInit2(ceph_proxy_op_t *op, const int64_t poolId, const char* oid);

void CephProxyReadOpRelease(ceph_proxy_op_t op);

void CephProxyReadOpSetFlags(ceph_proxy_op_t op, int flags);

void CephProxyReadOpAssertExists(ceph_proxy_op_t op);

void CephProxyReadOpAssertVersion(ceph_proxy_op_t op, uint64_t ver);

void CephProxyReadOpCmpext(ceph_proxy_op_t op, const char *cmpBuf, size_t cmpLen, uint64_t off, int *prval);

void CephProxyReadOpCmpXattr(ceph_proxy_op_t op,  const char *name, uint8_t compOperator, const char *value, size_t valueLen);

void CephProxyReadOpGetXattrs(ceph_proxy_op_t op, proxy_xattrs_iter_t *iter, int *prval);

void CephProxyReadOpOmapCmp(ceph_proxy_op_t op, const char *key, uint8_t compOperator, const char *val, size_t valLen, int *prval);

void CephProxyReadOpStat(ceph_proxy_op_t op, uint64_t *psize, time_t *pmtime, int *prval);

void CephProxyReadOpRead(ceph_proxy_op_t op, uint64_t offset, size_t len, char *buffer, size_t *bytesRead, int *prval);

void CephProxyReadOpReadSGL(ceph_proxy_op_t op, uint64_t offset, size_t len, SGL_S *sgl, int *prval);

void CephProxyReadOpCheckSum(ceph_proxy_op_t op, proxy_checksum_type_t type, const char *initValue, size_t initValueLen, uint64_t offset, size_t len, size_t chunkSize, char *pCheckSum, size_t checkSumLen, int *prval);

completion_t CephProxyCreateCompletion(CallBack_t fn, void *arg);

void CephProxyCompletionDestroy(completion_t c);

#ifdef __cplusplus
}
#endif

#endif

