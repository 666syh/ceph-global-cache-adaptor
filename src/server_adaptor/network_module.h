/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
 * 
 */

#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <queue>
#include <pthread.h>
#include <thread>
#include <string>
#include <vector>
#include <messages/MOSDOp.h>
#include <messages/MOSDOpReply.h>

#include "sa_server_dispatcher.h"
#include "sa_client_dispatcher.h"
#include "sa_def.h"
#include "client_op_queue.h" 
#include "msg_perf_record.h"
#include "sa_export.h"

class NetworkModule {
    SaExport *sa {nullptr};
    pthread_t serverThread { 0 };
    pthread_t clientThread { 0 };
    pthread_t transToOpreqThread { 0 } ;
    pthread_t sendOpreplyThread { 0 };

    bool startServerThread { false };
    bool startClientThread { false };
    bool startTranToOpreqThread { false };
    bool startSendOpreplyThread { false };

    entity_addr_t recvBindAddr;
    Messenger *clientMessenger { nullptr };
    SaClientDispatcher *clientDispatcher { nullptr };
    entity_addr_t sendBindAddr;

    std::string recvAddr { "localhost" };
    std::string recvPort { "1234" };
    std::string sendAddr { "localhost" };
    std::string sednPort { "1234" };

    MsgModule *ptrMsgModule { nullptr };
    std::queue<MOSDOp *> qReadyTransToOpreq {};
    std::queue<MOSDOpReply *> qSendToClientAdaptor {};

    bool testPing { false };
    bool testMosdop { false };

    uint64_t queueNum { 0 };
    uint32_t queueMaxCapacity { 0 };
    std::vector<std::thread> doOpThread {};
    std::vector<ClientOpQueue *> opDispatcher {};
    std::vector<bool> finishThread {};
    std::vector<int> coreId;
    uint32_t msgrNum { 5 };
    uint32_t bindMsgrCore { 0 };
    uint32_t bindSaCore { 0 };

    MsgPerfRecord *msgPerf { nullptr};

    std::vector<std::string> vecPorts;
    std::vector<Messenger *> vecSvrMessenger;
    std::vector<SaServerDispatcher *> vecDispatcher;

    int *bindSuccess { nullptr };









    int InitMessenger();









    int FinishMessenger();

    void BindMsgrWorker(pid_t pid);

    void BindCore(uint64_t tid, uint32_t seq, bool isWorker = true);

public:
    NetworkModule() = delete;

    explicit NetworkModule(SaExport &p, std::vector<int> &vec, uint32_t msgr, uint32_t bind, uint32_t bindsa)
    {
	    msgrNum = msgr;
	    coreId = vec;
	    bindMsgrCore = bind;
	    bindSaCore = bindsa;
	    sa = &p;
    }

    ~NetworkModule()
    {
        if (ptrMsgModule) {
            delete ptrMsgModule;
        }
	for (auto &i : vecSvrMessenger) {
		if (i) {
			delete i;
		}
	}
	for (auto &i : vecDispatcher) {
		if (i) {
			delete i;
		}
	}
        if (clientMessenger) {
            delete clientMessenger;
        }
        if (clientDispatcher) {
            delete clientDispatcher;
        }
        if (msgPerf) {
            delete msgPerf;
        }
    }











    int InitNetworkModule(const std::string &rAddr, const std::vector<std::string> &rPort, const std::string &sAddr,
        const std::string &sPort, int *bind);









    int FinishNetworkModule();









    int ThreadFuncBodyServer();









    int ThreadFuncBodyClient();

    void CreateWorkThread(uint32_t queueNum, uint32_t portAmout, uint32_t qmaxcapacity);
    void StopThread();
    void OpHandlerThread(int threadNum, int coreId);
    MsgModule *GetMsgModule()
    {
        return ptrMsgModule;
    }
    uint32_t EnqueueClientop(MOSDOp *opReq);

    void TestSimulateClient(bool ping, bool mosdop);
};

void FinishCacheOps(void *op, int32_t r);
void ProcessBuf(const char *buf, uint32_t len, int cnt, void *p);

void EncodeOmapGetkeys(const SaBatchKeys *batchKeys, int i, MOSDOp *p);
void EncodeOmapGetvals(const SaBatchKv *KVs, int i, MOSDOp *mosdop);
void EncodeOmapGetvalsbykeys(const SaBatchKv *keyValue, int i, MOSDOp *mosdop);
void EncodeRead(uint64_t opType, unsigned int offset, unsigned int len, char *buf, unsigned int bufLen, int i,
    MOSDOp *mosdop);
void SetOpResult(int i, int32_t ret, MOSDOp *op);
void EncodeXattrGetXattr(const SaBatchKv *keyValue, int i, MOSDOp *mosdop);
void EncodeXattrGetXattrs(const SaBatchKv *keyValue, int i, MOSDOp *mosdop);
void EncodeGetOpstat(uint64_t psize, time_t ptime, int i, MOSDOp *mosdop);
#endif
