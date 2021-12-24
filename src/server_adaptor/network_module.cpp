/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
 * Description: Interact with Messanger(ClientAdaptor) and CCM agent.
 */


#include "network_module.h"

#include <sys/types.h>
#include <iostream>
#include <string>
#include <sys/prctl.h>

#include "common/config.h"
#include "common/Timer.h"
#include "common/ceph_argparse.h"
#include "global/signal_handler.h"
#include "perfglue/heap_profiler.h"
#include "common/address_helper.h"
#include "auth/DummyAuth.h"
#include "msg/msg_types.h"
#include "messages/MPing.h"
#include "common/common_init.h"
#include "messages/MOSDOpReply.h"
#include "salog.h"
#include "sa_ftds_osa.h"

#define dout_subsys ceph_subsys_simple_client

using namespace std;

namespace {
const string LOG_TYPE = "NETWORK";
const char *SA_THREAD_NAME = "gc_sa";
MsgPerfRecord *g_msgPerf { nullptr };
const uint32_t ONE_THOUSAND = 1024;
}

void *ThreadServer(void *arg)
{
    static_cast<NetworkModule *>(arg)->ThreadFuncBodyServer();
    return nullptr;
}

void *ThreadClient(void *arg)
{
    static_cast<NetworkModule *>(arg)->ThreadFuncBodyClient();
    return nullptr;
}

int NetworkModule::InitNetworkModule(const std::string &rAddr, const std::vector<std::string> &rPort,
	       	const  std::string &sAddr, const std::string &sPort, int *bind)
{
    Salog(LV_DEBUG, LOG_TYPE, "Init network module.");
    int ret;
    if (ptrMsgModule == nullptr) {
        ptrMsgModule = new MsgModule();
    }
    recvAddr = rAddr;
    vecPorts = rPort;
    sendAddr = sAddr;
    sednPort = sPort;
    bindSuccess = bind;

    ret = InitMessenger();
    if (ret) {
        Salog(LV_DEBUG, LOG_TYPE, "error : Init messenger ret is %d", ret);
        return ret;
    }
#ifdef SA_PERF
    if (msgPerf == nullptr) {
	msgPerf = new MsgPerfRecord();
    }
    if (g_msgPerf == nullptr) {
	g_msgPerf = msgPerf;
    }
    msgPerf->start();
    Salog(LV_WARNING, LOG_TYPE, "SA_PERF open");
#endif
    return ret;
}

int NetworkModule::FinishNetworkModule()
{
    int ret = 0;
    ret = FinishMessenger();
    if (ret) {
        Salog(LV_DEBUG, LOG_TYPE, "FinishMessenger is failed ret=%d", ret);
    }
    Salog(LV_DEBUG, LOG_TYPE, "Finish network module.");
#ifdef SA_PERF
    msgPerf->stop();
    g_msgPerf = nullptr;
#endif
    return ret;
}

int NetworkModule::InitMessenger()
{
    int ret = 0;
    startServerThread = true;
    ret = pthread_create(&serverThread, nullptr, ThreadServer, this);
    if (ret) {
        Salog(LV_DEBUG, LOG_TYPE, "Creating ThreadServer is failed ret=%d", ret);
        startServerThread = false;
        return ret;
    }
    return ret;
}

int NetworkModule::FinishMessenger()
{
    int ret = 0;
    for (auto &i : vecSvrMessenger) {
        i->shutdown();
        // i->wait();
    }
    if (clientMessenger) {
        clientMessenger->shutdown();
        clientMessenger->wait();
    }
    Salog(LV_INFORMATION, LOG_TYPE, "Wait serverThread finish.");
    pthread_join(serverThread, nullptr);
    Salog(LV_INFORMATION, LOG_TYPE, "FinishMessenger ret=%d", ret);
    return ret;
}

int NetworkModule::ThreadFuncBodyServer()
{
    int r = 0;
    pid_t pid = getpid();
    DummyAuthClientServer dummy_auth(g_ceph_context);
    for (auto &i : vecPorts) {
	entity_addr_t bind_addr;
	string strPort = i;
	Salog(LV_WARNING, LOG_TYPE, "Server messanger is starting...");

        string dest_str = "tcp://";
        dest_str += recvAddr;
        dest_str += ":";
        dest_str += strPort;
        entity_addr_from_url(&bind_addr, dest_str.c_str());
        Salog(LV_WARNING, LOG_TYPE, "Messenger type is %s", g_conf().get_val<std::string>("ms_type").c_str());
        // async+posix
        Messenger *svrMessenger = Messenger::create(g_ceph_context, g_conf().get_val<std::string>("ms_type"),
            entity_name_t::OSD(-1), "simple_server", 0 /*nonce */, 0 /* flags */);

        svrMessenger->set_auth_server(&dummy_auth);
        svrMessenger->set_magic(MSG_MAGIC_TRACE_CTR);
        svrMessenger->set_default_policy(Messenger::Policy::stateless_server(0));








        bind_addr.set_type(entity_addr_t::TYPE_MSGR2);
        r = svrMessenger->bind(bind_addr);
        if (r < 0) {
	    Salog(LV_ERROR, LOG_TYPE, "bind error %s:%s", recvAddr.c_str(), strPort.c_str());
	    *bindSuccess = 0;
            goto out;
	}

    	SaServerDispatcher *svrDispatcher = new SaServerDispatcher(svrMessenger, ptrMsgModule, this);
	svrDispatcher->ms_set_require_authorizer(false);
        svrMessenger->add_dispatcher_head(svrDispatcher);
        svrMessenger->start();
        vecSvrMessenger.push_back(svrMessenger);
        vecDispatcher.push_back(svrDispatcher);
    }
    common_init_finish(g_ceph_context);

    if (bindMsgrCore) {
	BindMsgrWorker(pid);
	Salog(LV_WARNING, LOG_TYPE, "msgr-worker and ms_dispatch bind cores.", pid);
    }

    if (!vecPorts.empty()) {
	Salog(LV_WARNING, LOG_TYPE, "ServerMessenger wait");
	*bindSuccess = 1;
	vecSvrMessenger[0]->wait();
    }
out:
    Salog(LV_WARNING, LOG_TYPE, "Server exit");
    return r;
}

static int easy_readdir(const std::string &dir, std::set<std::string> *out)
{
    DIR *h = ::opendir(dir.c_str());
    if (!h) {
	return -errno;
    }
    struct dirent *de = nullptr;
    while ((de = ::readdir(h))) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
	    continue;
	}
	out->insert(de->d_name);
    }
    closedir(h);
    return 0;
}

void NetworkModule::BindMsgrWorker(pid_t pid)
{
    std::set<std::string> ls;
    char path[128] = {0};
    sprintf(path, "/proc/%d/task", pid);
    int r = easy_readdir(path, &ls);
    Salog(LV_INFORMATION, LOG_TYPE, "path:%s ls_size=%d", path, ls.size());
    vector<uint64_t> vecBindMsgr;
    vector<uint64_t> vecBindDispatch;
    for (auto &i : ls) {
	string readPath = path;
	readPath += "/" + i + "/status";
	FILE *fp = fopen(readPath.c_str(), "r");

	if (NULL == fp) {
	    Salog(LV_ERROR, LOG_TYPE, "open file error:%s", readPath.c_str());
	    return;
	}
	if (!feof(fp)) {
	    char line[200] = {0};
	    memset(line, 0, sizeof(line));
	    fgets(line, sizeof(line) - 1, fp);
	    string strLine = line;
	    if (strLine.find("msgr-worker-") != string::npos) {
		vecBindMsgr.push_back(atoll(i.c_str()));
	    }
	    if (strLine.find("ms_dispatch") != string::npos) {
		vecBindDispatch.push_back(atoll(i.c_str()));
	    }
	}

	fclose(fp);
    }
    sort(vecBindMsgr.rbegin(), vecBindMsgr.rend());
    sort(vecBindDispatch.rbegin(), vecBindDispatch.rend());
    vector<uint64_t> bindThreads;
    for (int i = 0; i< msgrNum; i++) {
	if (i < vecBindMsgr.size()) {
	    bindThreads.push_back(vecBindMsgr[i]);
	}
    }
    for (int i = 0; i< vecPorts.size(); i++) {
	if (i < vecBindDispatch.size()) {
	    bindThreads.push_back(vecBindDispatch[i]);
	}
    }
    int n = 0;
    uint32_t threadNum = msgrNum + vecPorts.size();
    Salog(LV_INFORMATION, LOG_TYPE, "msgrNum=%d dispatch=%d", msgrNum, vecPorts.size());
    for (auto &ii : bindThreads) {
	BindCore(ii, threadNum);
	n++;
	if (n >= threadNum) {
	    break;
	}
    }
    for (vector<uint64_t>::iterator ite = vecBindDispatch.begin(); ite != (vecBindDispatch.end() - 1); ite++) {

    }
}

void NetworkModule::BindCore(uint64_t ii, uint32_t n, bool isWorker)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    int cpuNum = 0;
    if (isWorker) {
	for (int i = 0; i < n; i++) {
	    cpuNum =coreId[i % coreId.size()];
	    Salog(LV_WARNING, LOG_TYPE, "isWorker %d cpuId=%d", ii, cpuNum);
	    CPU_SET(cpuNum, &mask);
	}
    } else {
        cpuNum =coreId[n % coreId.size()];
        Salog(LV_WARNING, LOG_TYPE, "isDispatch %d cpuId=%d", ii, cpuNum);
        CPU_SET(cpuNum, &mask);
    }
    pid_t tid = ii;
    if (sched_setaffinity(tid, sizeof(mask), &mask) == -1) {
	 Salog(LV_ERROR, LOG_TYPE, "setaffinity failed %ld", ii);
	 return;
    }

    cpu_set_t getMask;
    CPU_ZERO(&getMask);
    if (sched_getaffinity(tid, sizeof(getMask), &getMask) == -1) {
	 Salog(LV_ERROR, LOG_TYPE, "getaffinity failed");
    }
    int cpus = sysconf(_SC_NPROCESSORS_CONF);
    for (int i = 0; i < cpus; i++) {
	 if (CPU_ISSET(i, &getMask)) {
	    Salog(LV_WARNING, LOG_TYPE, "this thread %ld running processor:%d", ii, i);
	 }
    }
}
	
int NetworkModule::ThreadFuncBodyClient()
{
   ConnectionRef conn;
    int r = 0;
    int n_msgs = 10;
    int n_dsize = 0;

    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    cout << "ThreadFuncBodyClient starting "
         << "dest sendAddr " << sendAddr << " "
         << "dest sednPort " << sednPort << " "
         << "initial msgs (pipe depth) " << n_msgs << " "
         << "data buffer size " << n_dsize << std::endl;
    cout << "ThreadFuncBodyClient ms_type=" << g_conf().get_val<std::string>("ms_type") << std::endl;
    clientMessenger = Messenger::create(g_ceph_context, g_conf().get_val<std::string>("ms_type"),
        entity_name_t::CLIENT(-1), "client", getpid(), 0);

    DummyAuthClientServer dummy_auth(g_ceph_context);
    clientMessenger->set_auth_client(&dummy_auth);
    clientMessenger->set_magic(MSG_MAGIC_TRACE_CTR);
    clientMessenger->set_default_policy(Messenger::Policy::lossy_client(0));


    clientDispatcher = new SaClientDispatcher(clientMessenger, ptrMsgModule);
    clientDispatcher->ms_set_require_authorizer(false);
    clientMessenger->add_dispatcher_head(clientDispatcher);

    clientDispatcher->set_active();

    r = clientMessenger->start();
    if (r < 0)
        goto out;

    time_t t1, t2;
    t1 = time(NULL);

    if (testPing || testMosdop) {
        entity_addr_t dest_addr;
        string dest_str = "tcp://";
        dest_str += sendAddr;
        dest_str += ":";
        dest_str += sednPort;
        entity_addr_from_url(&dest_addr, dest_str.c_str());
        dest_addr.set_type(entity_addr_t::TYPE_MSGR2);
        entity_addrvec_t dest_addrs(dest_addr);
        conn = clientMessenger->connect_to_osd(dest_addrs);
        while (!conn->is_connected()) {
            nanosleep(&ts, NULL);
        }

        if (testPing) {
            cerr << "ThreadFuncBodyClient send ping." << std::endl;
            n_msgs = 5;
            for (int msg_ix = 0; msg_ix < n_msgs; ++msg_ix) {
                /* add a data payload if asked */
                Message *m = new MPing();
                cerr << "TEST: ClientAdaptor send ping " << msg_ix << std::endl;
                conn->send_message(m);
            }
        }

        if (testMosdop) {
            cerr << "ThreadFuncBodyClient send MOSDOp" << std::endl;
            n_msgs = 10;
            for (int msg_ix = 0; msg_ix < n_msgs; ++msg_ix) {
                std::atomic<unsigned> client_inc = { 0 };

                object_t oid("object-name");
                object_locator_t oloc(1, 1);
                pg_t pgid;
                hobject_t hobj(oid, oloc.key, CEPH_NOSNAP, pgid.ps(), pgid.pool(), oloc.nspace);

                spg_t spgid(pgid);

                MOSDOp *mosdop = new MOSDOp(client_inc, 0, hobj, spgid, 0, 0, 0);
                cerr << "TEST: ClientAdaptor send MOSDOp " << msg_ix << std::endl;
                conn->send_message(mosdop);
            }
        }
    }

    // do stuff
    cout << "Connection is not connected." << std::endl;
    t2 = time(NULL);
    cout << "Processed " << clientDispatcher->get_dcount() + n_msgs << " round-trip messages in " << t2 - t1 << "s" <<
        std::endl;
out:
    return r;
}

void NetworkModule::TestSimulateClient(bool ping, bool mosdop)
{
    testPing = ping;
    testMosdop = mosdop;
    startClientThread = true;
    int ret = pthread_create(&clientThread, nullptr, ThreadClient, this);
    if (ret) {
        std::cerr << "Creating ThreadClient is failed ret= " << ret << std::endl;
        startClientThread = false;
    }

    cout << "TestSimulateClient wait clientThread finish." << std::endl;
    pthread_join(clientThread, nullptr);
    cout << "TestSimulateClient is finished." << std::endl;
}
void *ThreadFunc(NetworkModule *arg, int threadNum, int coreId)
{
    arg->OpHandlerThread(threadNum, coreId);
    return nullptr;
}

void NetworkModule::CreateWorkThread(uint32_t qnum, uint32_t portAmout, uint32_t qmaxcapacity)
{
    finishThread.clear();
    opDispatcher.clear();
    doOpThread.clear();
    queueNum = qnum;
    queueMaxCapacity = qmaxcapacity;
    vector<int> saCoreId;
    if (bindMsgrCore) {
	if ((msgrNum + portAmout) >= coreId.size()) {
   	    Salog(LV_WARNING, LOG_TYPE, "msgrNum=%d coreId.size=%d", msgrNum, coreId.size());
	    saCoreId = coreId;
	} else {
	    for (int i = msgrNum + portAmout; i < coreId.size(); i++) {
		Salog(LV_WARNING, LOG_TYPE, "push coreId[i]=%d", coreId[i]);
		saCoreId.push_back(coreId[i]);
	    }
	}
    } else {
	saCoreId = coreId;
    }

    for (uint64_t i = 0; i < queueNum; i++) {
        finishThread.push_back(false);
        opDispatcher.push_back(new ClientOpQueue());
	int cpuNum = saCoreId[i % saCoreId.size()];
        doOpThread.push_back(thread(ThreadFunc, this, i, cpuNum));
    }
    Salog(LV_WARNING, LOG_TYPE, "CreateWorkThread %d %d", queueNum, qmaxcapacity);
}

void NetworkModule::StopThread()
{
    for (uint32_t i = 0; i < finishThread.size(); i++) {
        std::unique_lock<std::mutex> opReqLock(opDispatcher[i]->opQueueMutex);
        finishThread[i] = true;
    }

    for (uint32_t i = 0; i < opDispatcher.size(); i++) {
        opDispatcher[i]->condOpReq.notify_all();
    }

    for (uint32_t i = 0; i < doOpThread.size(); i++) {
        doOpThread[i].join();
    }
}

void NetworkModule::OpHandlerThread(int threadNum, int coreId)
{
    if (bindSaCore) {

	Salog(LV_DEBUG, LOG_TYPE, "bind_gc_sa cpuId=%d", coreId);
        int cpus = sysconf(_SC_NPROCESSORS_CONF);
        Salog(LV_WARNING, LOG_TYPE, "core affinity cpus=%d coreId=%d", cpus, coreId);
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(coreId, &mask);
        if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
            Salog(LV_WARNING, LOG_TYPE, "setaffinity failed");
        }
        cpu_set_t getMask;
        CPU_ZERO(&getMask);
        if (sched_getaffinity(0, sizeof(getMask), &getMask) == -1) {
            Salog(LV_WARNING, LOG_TYPE, "getaffinity failed");
        }

        for (int i = 0; i < cpus; i++) {
            if (CPU_ISSET(i, &getMask)) {
                Salog(LV_INFORMATION, LOG_TYPE, "this process %d of running processor: %d\n", getpid(), i);
	    }
        }
    }
    prctl(PR_SET_NAME, SA_THREAD_NAME);
    int threadId = threadNum;
    ClientOpQueue *opDispatch = opDispatcher[threadId];
    std::queue<MOSDOp *> dealQueue;
    std::queue<uint64_t> dealTs;
    std::queue<uint64_t> periodTs; 
    std::unique_lock<std::mutex> opReqLock(opDispatch->opQueueMutex);
    while (!finishThread[threadId]) {
        if (!opDispatch->Empty()) {
            opDispatch->reqQueue.swap(dealQueue);
	    Salog(LV_DEBUG, LOG_TYPE, "queue_size_swaped %d", dealQueue.size());
	    opDispatch->tsQueue.swap(dealTs);
	    opDispatch->period.swap(periodTs);
	    opDispatch->cond.notify_all();
            opReqLock.unlock();
            //
            while (!dealQueue.empty()) {
                MOSDOp *op = dealQueue.front();
                dealQueue.pop();

		uint64_t ts = dealTs.front();
		dealTs.pop();

		uint64_t pts = periodTs.front();
		periodTs.pop();
		sa->FtdsEndHigt(SA_FTDS_QUEUE_PERIOD, pts, 0);
		uint64_t transTs = 0;
		sa->FtdsStartHigh(SA_FTDS_TRANS_OPREQ, transTs);
                SaOpReq *opreq = new SaOpReq;
                opreq->opType = OBJECT_OP;
                opreq->snapId = op->get_snapid();
                opreq->poolId = op->get_pg().pool();
                opreq->opsSequence = op->get_header().seq;
                opreq->ptrMosdop = op;
                opreq->ptId = op->get_pg().m_seed;

                vector<char *>vecObj;
                const char *delim = ".";
                std::unique_ptr<char[]> tmp = std::make_unique<char[]>(op->get_oid().name.size() + 1);
                strcpy(tmp.get(), op->get_oid().name.c_str());
                char *p;
                char *savep;
                p = strtok_r(tmp.get(), delim, &savep);
                while (p) {
                    vecObj.push_back(p);
                    p = strtok_r(nullptr, delim, &savep);
                }
                bool isRbd = false;
                if (vecObj.empty() == false && strcmp(vecObj[0], "rbd_data") == 0) {
                    if (vecObj.size() >= 3) {
                        isRbd = true;
                    } else {
                        Salog(LV_CRITICAL, LOG_TYPE, "rbd_obj_id is %d sections, this op return -EINVAL",
                           vecObj.size());
                        FinishCacheOps(op, -EINVAL);
                        continue;
                    }
                }
		OptionsType optionType = { 0 };
                for (auto &i : op->ops) {
                    OpRequestOps oneOp;
                    oneOp.objName = op->get_oid().name;
                    if (isRbd) {
                        oneOp.isRbd = isRbd;
                        oneOp.rbdObjId.head = strtoul(vecObj[vecObj.size() - 2], 0, 16);
                        oneOp.rbdObjId.sequence = strtoul(vecObj[vecObj.size() - 1], 0, 16);
                    }
                    GetMsgModule()->ConvertClientopToOpreq(i, oneOp, optionType);
                    opreq->vecOps.push_back(oneOp);
                    Salog(LV_DEBUG, LOG_TYPE, "isRbd=%d obj_name=%s head=%ld sequence=%ld ptid=%d", isRbd,
                        op->get_oid().name.c_str(), oneOp.rbdObjId.head, oneOp.rbdObjId.sequence, opreq->ptId);
                }

		if (unlikely(optionType.write <= optionType.read)) {
			opreq->optionType = GCACHE_READ;
		} else {
			opreq->optionType = GCACHE_WRITE;
		}

		sa->FtdsEndHigt(SA_FTDS_TRANS_OPREQ, transTs, 0);
		sa->DoOneOps(*opreq);

		sa->FtdsEndHigt(SA_FTDS_OP_LIFE, ts, 0);
            }
	    uint64_t lockTsOne = 0;
	    sa->FtdsStartHigh(SA_FTDS_LOCK_ONE, lockTsOne);
            opReqLock.lock();
	    sa->FtdsEndHigt(SA_FTDS_LOCK_ONE, lockTsOne, 0);
            continue;
        }
        opDispatch->condOpReq.wait(opReqLock);
    }
    Salog(LV_DEBUG, "OpHandler", "OpHandlerThread  Finish");
}

uint32_t NetworkModule::EnqueueClientop(MOSDOp *opReq)
{
    string source;
#ifdef SA_PERF
    msgPerf->set_recv(opReq->osa_tick.SetRecvEnd(opReq, source));
#endif
    int ret = 0;
    uint64_t ts = 0;
    sa->FtdsStartHigh(SA_FTDS_OP_LIFE, ts);
    uint64_t enqueTs = 0;
    sa->FtdsStartHigh(SA_FTDS_MOSDOP_ENQUEUE, enqueTs);

    opReq->finish_decode();
    size_t idx = std::hash<std::string> {}(opReq->get_oid().name) % queueNum;
    std::unique_lock<std::mutex> opReqLock(opDispatcher[idx]->opQueueMutex);
    uint64_t periodTs = 0;
    sa->FtdsStartHigh(SA_FTDS_QUEUE_PERIOD, periodTs);
    if (opDispatcher[idx]->GetSize() > queueMaxCapacity) {
        SalogLimit(LV_WARNING, LOG_TYPE, "%d queue_capacity_is_large. %d %d", idx, opDispatcher[idx]->GetSize());
        opDispatcher[idx]->cond.wait(opReqLock);
    }
    opDispatcher[idx]->EnQueue(opReq, ts, periodTs);
    Salog(LV_DEBUG, LOG_TYPE, "MOSDOp is in the queue.  vec_index=%ld", idx);
    sa->FtdsEndHigt(SA_FTDS_MOSDOP_ENQUEUE, enqueTs, 0);
    return ret;
}

void FinishCacheOps(void *op, int32_t r)
{
    MOSDOp *ptr = (MOSDOp *)(op);
    MOSDOpReply *reply = new MOSDOpReply(ptr, 0, 0, 0, false);
    reply->claim_op_out_data(ptr->ops);
    reply->set_result(r);
    reply->add_flags(CEPH_OSD_FLAG_ACK | CEPH_OSD_FLAG_ONDISK);
    ConnectionRef con = ptr->get_connection();
#ifdef SA_PERF
    ptr->osa_tick.SetSendStart(ptr);
#endif
    con->send_message(reply);
    string source;
#ifdef SA_PERF
    g_msgPerf->set_send(ptr->osa_tick.SetSendEnd(ptr, source));
    g_msgPerf->set_Total(ptr->osa_tick.GetMsgLife(ptr));
#endif
    ptr->put();
}

void SetOpResult(int i, int32_t ret, MOSDOp *op)
{
    op->ops[i].rval = ret;
}

void ProcessBuf(const char *buf, uint32_t len, int cnt, void *p)
{
    MOSDOp *ptr = (MOSDOp *)(p);
    encode(std::string_view(buf, len), ptr->ops[cnt].outdata);
}

void EncodeOmapGetkeys(const SaBatchKeys *batchKeys, int i, MOSDOp *mosdop)
{
    bufferlist bl;
    for (uint32_t j = 0; j < batchKeys->nums; j++) {
        encode(std::string_view(batchKeys->keys[j].buf, batchKeys->keys[j].len), bl);
    }
    encode(batchKeys->nums, mosdop->ops[i].outdata);
    Salog(LV_DEBUG, LOG_TYPE, "CEPH_OSD_OP_OMAPGETKEYS get key num=%d", batchKeys->nums);
    mosdop->ops[i].outdata.claim_append(bl);
    // TODO
    encode(false, mosdop->ops[i].outdata);
}

void EncodeOmapGetvals(const SaBatchKv *KVs, int i, MOSDOp *mosdop)
{
    bufferlist bl;
    Salog(LV_DEBUG, LOG_TYPE, "CEPH_OSD_OP_OMAPGETVALS get key num=%d", KVs->kvNum);
    for (uint32_t j = 0; j < KVs->kvNum; j++) {
        if (KVs->keys[j].buf && KVs->keys[j].len) {
            Salog(LV_DEBUG, LOG_TYPE, "CEPH_OSD_OP_OMAPGETVALS get key KVs->keys[j].buf=%s", KVs->keys[j].buf);
            encode(std::string_view(KVs->keys[j].buf, KVs->keys[j].len), bl);
        }
        if (KVs->values[j].buf && KVs->values[j].len) {
            Salog(LV_DEBUG, LOG_TYPE, "CEPH_OSD_OP_OMAPGETVALS get key KVs->keys[j].buf=%s", KVs->values[j].buf);
            encode(std::string_view(KVs->values[j].buf, KVs->values[j].len), bl);
        }
    }
    encode(KVs->kvNum, mosdop->ops[i].outdata);
    mosdop->ops[i].outdata.claim_append(bl);
    encode(false, mosdop->ops[i].outdata);
}

void EncodeOmapGetvalsbykeys(const SaBatchKv *keyValue, int i, MOSDOp *mosdop)
{
    map<string, bufferlist> out;
    for (uint32_t j = 0; j < keyValue->kvNum; j++) {
        bufferlist value;
        string keys(keyValue->keys[j].buf, keyValue->keys[j].len);
        value.append(keyValue->values[j].buf, keyValue->values[j].len);
        out.insert(make_pair(keys, value));
    }
    encode(out, mosdop->ops[i].outdata);
}


//
void EncodeRead(uint64_t opType, unsigned int offset, unsigned int len, char *buf, unsigned int bufLen, int i,
    MOSDOp *mosdop)
{
    if (unlikely(opType == CEPH_OSD_OP_SPARSE_READ)) {
        std::map<uint64_t, uint64_t> extents;
        extents[offset] = len;
        encode(extents, mosdop->ops[i].outdata);
        encode(std::string_view(buf, bufLen), mosdop->ops[i].outdata);
    } else {
        mosdop->ops[i].outdata.append(buf, bufLen);
    }
}

void EncodeXattrGetXattr(const SaBatchKv *keyValue, int i, MOSDOp *mosdop)
{
    mosdop->ops[i].outdata.clear();
    for (uint32_t j = 0; j < keyValue->kvNum; j++) {
        bufferptr ptr(keyValue->values[j].buf, keyValue->values[j].len);
        mosdop->ops[i].outdata.push_back(std::move(ptr));
    }
}

void EncodeXattrGetXattrs(const SaBatchKv *keyValue, int i, MOSDOp *mosdop)
{
    map<string, bufferlist> out;
    bufferlist bl;
    for (uint32_t j = 0; j < keyValue->kvNum; j++) {
        bufferlist value;
        string keys(keyValue->keys[j].buf, keyValue->keys[j].len);
        value.append(keyValue->values[j].buf, keyValue->values[j].len);
        out.insert(make_pair(keys, value));
    }
    encode(out, bl);
    mosdop->ops[i].outdata.claim_append(bl);
}

void EncodeGetOpstat(uint64_t psize, time_t ptime, int i, MOSDOp *mosdop)
{
    encode(psize, mosdop->ops[i].outdata);
    encode(ptime, mosdop->ops[i].outdata);
}
