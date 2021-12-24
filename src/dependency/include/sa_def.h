/*
 *











 *
 */

#ifndef SA_DEF_H
#define SA_DEF_H

#include <string>
#include <vector>

constexpr int MY_PID = 666;

constexpr uint32_t MAX_PT_NUMBER = 128;

constexpr int INDEX_ZERO = 0;
constexpr int INDEX_ONE = 1;
constexpr int INDEX_TWO = 2;
constexpr int INDEX_THREE = 3;

constexpr int OBJECT_OP = 0;
constexpr int POOL_OP = 1;

constexpr int GCACHE_DEFAULT = 0;
constexpr int GCACHE_WRITE = 1;
constexpr int GCACHE_READ = 2;

struct OptionsType {
    uint32_t write;
    uint32_t read;
};

struct RbdObjid {
    RbdObjid()
    {
	rbdd[INDEX_ZERO] = 'R';
	rbdd[INDEX_ONE] = 'b';
	rbdd[INDEX_TWO] = 'd';
	rbdd[INDEX_THREE] = 'd';
    }
    char rbdd[4];
    uint64_t head { 0 };
    uint64_t sequence { 0 };
} __attribute__((packed, aligned(4)));

struct OpRequestOps {
    uint64_t opSubType {0};

    std::string objName {};
    bool isRbd { false };
    RbdObjid rbdObjId;

    char *inData { nullptr };

    uint32_t inDataLen { 0 };

    char *outData { nullptr };

    uint32_t outDataLen { 0 };

    uint32_t objOffset { 0 };

    uint32_t objLength { 0 };

    std::vector<std::string> keys {};
    
    std::vector<std::string> values {};
    std::vector<int> subops {};

    std::vector<uint64_t> u64vals {};
    std::vector<int> cmpModes {};
};


struct SaOpReq {
    std::vector<OpRequestOps> vecOps;

    uint32_t optionType { 0 };
    uint64_t opType { 0 };

    uint64_t snapId { 0 };

    uint64_t opsSequence { 0 };
    void *ptrMosdop { nullptr };

    uint32_t ptId { 0 };

    uint64_t poolId { 0 };

    uint64_t ts { 0 };
    SaOpReq &operator = (const struct SaOpReq &other)
    {
	if (this == &other) {
	    return *this;
	}
	optionType = other.optionType;
	opType = other.opType;
	snapId = other.snapId;
	opsSequence = other.opsSequence;
	ptrMosdop = other.ptrMosdop;
	ptId = other.ptId;
	poolId = other.poolId;
	ts = other.ts;
	return *this;
    }

    SaOpReq(const struct SaOpReq &other)
    {
        optionType=other.optionType;
	opType=other.opType;
	snapId=other.snapId;
	opsSequence=other.opsSequence;
	ptrMosdop=other.ptrMosdop;
	ptId=other.ptId;
	poolId=other.poolId;
	ts=other.ts;
    }
    SaOpReq() {}
};

struct SaOpContext {
    struct SaOpReq *opReq;
    int opId;
    int (*cbFunc)(struct SaOpReq *opReq);
};

using PREFETCH_FUNC = int (*)(struct SaOpReq *opReq, OpRequestOps *osdop);

struct SaStr {
    uint32_t len;
    char *buf;
};

struct SaBatchKv {
    SaStr *keys;
    SaStr *values;
    uint32_t kvNum;
};

struct SaKeyValue {
    uint32_t klen;
    char *kbuf;
    uint32_t vlen;
    char *vbuf;
};

struct SaBatchKeys {
    SaStr *keys;
    uint32_t nums;
};
#endif

