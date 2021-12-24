





#include "sa_client_dispatcher.h"
#include "salog.h"
#include "messages/MPing.h"
#include "messages/MDataPing.h"

using namespace std;

namespace{
const string LOG_TYPE = "CLNT_Dispatcher";
}

SaClientDispatcher::SaClientDispatcher(Messenger *msgr, MsgModule *msgModule)
    : Dispatcher(msgr->cct), active(false), messenger(msgr), dcount(0), ptrMsgModule(msgModule)
{}

SaClientDispatcher::~SaClientDispatcher() {}

bool SaClientDispatcher::ms_dispatch(Message *m)
{
    uint64_t dc = dcount++;
    
    ConnectionRef con = m->get_connection();

    switch (m->get_type()) {
	case CEPH_MSG_PING: {
	    if (unlikely(dc % 65536) == 0) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME_COARSE, &ts);
		cerr << "Client CEPH_MSG_PING " << dc << " nanos: " << ts.tv_nsec + (ts.tv_sec * 1000000000) <<
		       	" type=" << m->get_type() << std::endl;
		Salog(LV_DEBUG, LOG_TYPE, "CEPH_MSG_PING nanos:%ld", ts.tv_nsec+(ts.tv_sec*1000000000));
	    }
	    con->send_message(m);
	} break;
	case CEPH_MSG_OSD_OPREPLY: {
		cerr <<"ClientAdaptor recieve CEPH_MSG_OSD_OPREPLY " << dc << std::endl;
		Salog(LV_DEBUG, LOG_TYPE,"CEPH_MSG_OSD_OPREPLY");

		m->put();
	} break;
	default: {
		Salog(LV_DEBUG, LOG_TYPE,"Client dispatch unknown message type %d", m->get_type());

	}
    }

    return true;
}

bool SaClientDispatcher::ms_handle_reset(Connection *con)
{
	return true;
}

void SaClientDispatcher::ms_handle_remote_reset(Connection *con) {}
