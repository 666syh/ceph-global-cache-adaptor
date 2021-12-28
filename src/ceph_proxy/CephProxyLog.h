/*
* Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*	http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __CEPH_PROXY_LOG_H__
#define __CEPH_PROXY_LOG_H__

#if defined DPAX_LOG
#include "dplog.h"
#ifndef MY_PID
#define MY_PID 882
#endif

#define ProxyDbgCrit(format, ...)                    \
	do {						\
		GCI_LOGGER_CRITICAL(MYID, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogErr(format, ...)                    \
	do {						\
		GCI_LOGGER_ERROR(MY_PID, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogWarn(format, ...)                    \
	do {						\
		GCI_LOGGER_WARN(MY_PID, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogInfo(format, ...)                    \
	do {						\
		GCI_LOGGER_INFO(MY_PID, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogDebug(format, ...)                    \
	do {						\
		GCI_LOGGER_DEBUG(MY_PID, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)
#elif defined SYS_LOG
#include <syslog.h>

#define ProxyDbgCrit(format, ...)                    \
	do {						\
		syslog(LOG_CRIT, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogErr(format, ...)                    \
	do {						\
		syslog(LOG_ERR, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogWarn(format, ...)                    \
	do {						\
		syslog(LOG_WARNING, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogInfo(format, ...)                    \
	do {						\
		 syslog(LOG_INFO, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)

#define ProxyDbgLogDebug(format, ...)                    \
	do {						\
		syslog(LOG_DEBUG, "[PROXY]" format, ##__VA_ARGS__);	\
	} while(0)
#else

#define ProxyDbgCrit(fmt, ...)                    \
	fprintf(stderr, "[%s][%d][%s][CRI][PROXY][" fmt "]\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define ProxyDbgLogErr(fmt, ...)                    \
	fprintf(stderr, "[%s][%d][%s][Err][PROXY][" fmt "]\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define ProxyDbgLogWarn(fmt, ...)                    \
	fprintf(stderr, "[%s][%d][%s][Warn][PROXY][" fmt "]\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define ProxyDbgLogInfo(fmt, ...)                    \
	fprintf(stderr, "[%s][%d][%s][Info][PROXY][" fmt "]\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define ProxyDbgLogDebug(fmt, ...)                    \
	fprintf(stderr, "[%s][%d][%s][Debug][PROXY][" fmt "]\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#endif

#endif

