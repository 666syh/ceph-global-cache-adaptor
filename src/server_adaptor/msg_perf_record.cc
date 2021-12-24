















#include "msg_perf_record.h"
#include <string>
#include <unistd.h>
#include <ctime>

#include <sys/syscall.h>
#define gettid() syscall(__NR_gettid)

using namespace std;

namespace {
const unsigned char SHORT_BUFF_LEN = 128;
}

void MsgPerfRecord::set_recv(long t)
{
     msgRecvCount++;
     msgRecvTinc += t;
}

void MsgPerfRecord::set_send(long t)
{
     msgSendCount++;
     msgSendTinc += t;
}

void MsgPerfRecord::set_Total(long t)
{
     msgTotalCount++;
     msgTotalTinc += t;
}

std::function<void()> MsgPerfRecord::write_perf_log()
{
    return [this]() {
        string threadName = "sa-msg-perf";
	pthread_setname_np(pthread_self(), threadName.c_str());
	std::cout << "Server Adaptor: msg performance tick start" << std::endl;

	time_t now = time(0);
	tm *ltm = localtime(&now);
	char fileName[SHORT_BUFF_LEN] = { 0 };
	sprintf(fileName, "/var/log/gcache/sa_msg_perf.%04d%02d%02d-%02d%02d%02d.log", (1900 + ltm->tm_year),
	    (1 + ltm->tm_mon), ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

	outfile.open(fileName, ios::out | ios::app);
	outfile << "***************str**************" << std::endl;
	while (!finish) {
	    sleep(3);
	    if (msgRecvCountPre == msgRecvCount && msgSendCountPre == msgSendCount &&
		msgTotalCountPre == msgTotalCount) {
	        continue;
	    }
	    msgRecvCountPre = msgRecvCount;
	    msgSendCountPre = msgSendCount;
	    msgTotalCountPre = msgTotalCount;
	    time_t writeTime = time(0);
	    tm *writeTimeTm = localtime(&writeTime);
	    char curTime[SHORT_BUFF_LEN] = { 0 };
	    sprintf(curTime, "%02d:%02d:%02d", writeTimeTm->tm_hour, writeTimeTm->tm_min, writeTimeTm->tm_sec);
	    outfile << curTime << " *********************************************" << std::endl;
	    outfile << "msgRecvCount = " << msgRecvCount << setw(16) << "  avg_recv_lat = " << msgRecvTinc / msgRecvCount << std::endl;
	    outfile << "msgSendCount = " << msgSendCount << setw(16) << "  avg_send_lat = " << msgSendTinc / msgSendCount << std::endl;
	    outfile << "msgTotalCount = " << msgTotalCount << setw(16) << "  avg_Total_lat = " << msgTotalTinc / msgTotalCount << std::endl;
	}
	outfile << "****************finish********************" << std::endl;
	outfile.close();
    };
}

void MsgPerfRecord::start()
{
    std::function<void()> perf_thread = write_perf_log();
    threads.push_back(std::thread(perf_thread));
}

void MsgPerfRecord::stop()
{
    finish = true;
    for (auto &i : threads) {
        i.join();
    }
}


















