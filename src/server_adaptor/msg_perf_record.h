















#ifndef MSG_PERF_RECORD_H
#define MSG_PERF_RECORD_H

#include <stdint.h>
#include <sys/time.h>
#include <thread>
#include <iomanip>
#include <sched.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <atomic>
#include <functional>

using namespace std;

class MsgPerfRecord {
    std::atomic<unsigned long long> msgRecvTinc;
    std::atomic<unsigned long long> msgRecvCount;
    unsigned long long msgRecvCountPre;

    std::atomic<unsigned long long> msgSendTinc;
    std::atomic<unsigned long long> msgSendCount;
    unsigned long long msgSendCountPre;

    std::atomic<unsigned long long> msgTotalTinc;
    std::atomic<unsigned long long> msgTotalCount;
    unsigned long long msgTotalCountPre;

    std::atomic<bool> finish;
    std::ofstream outfile;
    vector<std::thread> threads;

public:
    MsgPerfRecord()
    {
        msgRecvTinc = 0;
	msgRecvCount = 0;
	msgRecvCountPre = 0;

	msgSendTinc = 0;
	msgSendCount = 0;
	msgSendCountPre = 0;
	
	msgTotalTinc = 0;
	msgTotalCount = 0;
	msgTotalCountPre = 0;

	finish = false;
    }
    ~MsgPerfRecord() {}

    void set_recv(long t);
    void set_send(long t);
    void set_Total(long t);

    std::function<void()> write_perf_log();

    void start();
    void stop();
};


#endif
