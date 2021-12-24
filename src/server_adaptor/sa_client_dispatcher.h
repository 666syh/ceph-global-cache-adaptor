





#ifndef SA_CLIENT_DISPATCHER_H
#define SA_CLIENT_DISPATCHER_H

#include "msg/Dispatcher.h"
#include "msg/Messenger.h"

#include "msg_module.h"

class SaClientDispatcher : public Dispatcher {
	bool active;
	Messenger *messenger;
	uint64_t dcount;
	MsgModule *ptrMsgModule;
	int magicNum { 0 };

public:
	SaClientDispatcher() = delete;
	explicit SaClientDispatcher(Messenger *msgr, MsgModule *msgModule);
	~SaClientDispatcher() override;

	uint64_t get_dcount()
	{
		return dcount;
	}
	void set_active()
	{
		active = true;
	}


	bool ms_dispatch(Message *m) override;








	void ms_handle_connect(Connection *con) override {};






	void ms_handle_accept(Connection *con) override {};











	bool ms_handle_reset(Connection *con) override;










	void ms_handle_remote_reset(Connection *con) override;
	
	bool ms_handle_refused(Connection *con) override
	{
		return false;
	}
















	bool ms_get_authorizer(int dest_type, AuthAuthorizer **a) override
	{
		return false;
	};

	int ms_handle_authentication(Connection *con) override
	{
		return 1;
	}
};


#endif
	
