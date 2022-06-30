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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ConfigRead.h"
#include "CephProxyLog.h"

#define CLUSTER_CONFIG_FILE 	"/opt/gcache/conf/proxy.conf"
#define DEFAULT_CEPH_CONF_PATH 	"/etc/ceph/ceph.conf"
#define DEFAULT_LOG_PATH	"/var/log/gcache/proxy.log"
#define DEFAULT_ZK_SERVER_LIST	"localhost:2181"
#define DEFAULT_CORE_ARRAY	"26,27,28,29"

#define DEFAULT_WORKER_NUM	1
#define DEFAULT_MSGR_NUM	3
#define DEFAULT_BIND_CORE	0

#define CONFIG_BUFFSIZE 500
#define MAX_CPU_NUM (256)

static const char *ZK_SERVER_LIST = "zk_server_list";
static const char *CEPH_CONF_PATH = "ceph_conf_path";
static const char *LOG_OUT_PATH	  = "rados_log_out_file";
static const char *WORKER_NUM	  = "worker_num";
static const char *CORE_NUMBER	  = "core_number";
static const char *MSGR_AMOUNT	  = "msgr_amount";
static const char *BIND_CORE	  = "bind_core";
static const char *RADOS_MON_OP_TIMEOUT	= "rados_mon_op_timeout";
static const char *RADOS_OSD_OP_TIMEOUT	= "rados_osd_op_timeout";

#ifndef RETURN_OK
#define RETURN_OK 0
#endif

#ifndef RETURN_ERROR
#define RETURN_ERROR (-1)
#endif

typedef struct {
    char zkServerList[ZK_SERVER_LIST_STR_LEN + 1];
    char cephConfPath[MAX_PATH_LEN + 1];
    char logOutPath[MAX_PATH_LEN + 1];
    char coreIds[MAX_CORE_NUMBER + 1];

    uint64_t workerNum;
    uint64_t msgrAmount;
    uint64_t bindCore;
    uint64_t osdTimeout;
    uint64_t monTimeout;
} ProxyControlCfg;

ProxyControlCfg g_ProxyCfg = { 0 };

static void InitClusterCfg()
{
    sprintf(g_ProxyCfg.zkServerList, "%s", DEFAULT_ZK_SERVER_LIST);
    sprintf(g_ProxyCfg.cephConfPath, "%s", DEFAULT_CEPH_CONF_PATH);
    sprintf(g_ProxyCfg.logOutPath, "%s", DEFAULT_LOG_PATH);
    sprintf(g_ProxyCfg.coreIds, "%s", DEFAULT_CORE_ARRAY);

    g_ProxyCfg.bindCore = DEFAULT_BIND_CORE;
    g_ProxyCfg.workerNum = DEFAULT_WORKER_NUM;
    g_ProxyCfg.msgrAmount = DEFAULT_MSGR_NUM;

    g_ProxyCfg.osdTimeout = 0;
    g_ProxyCfg.monTimeout = 0;
}

static int32_t ConfigReadByLine(FILE *fptemp, uint8_t *buf, int32_t readLine)
{
    uint8_t *pcret = NULL;
    pcret = (uint8_t *)fgets((char *)buf, readLine, fptemp);
    if (pcret == NULL) {
        return RETURN_ERROR;
    } else {
        return RETURN_OK;
    }
}

static uint8_t *SearchSubString(uint8_t *buf, const char *substr)
{
    uint8_t *pstrret = NULL;
    if ((buf == NULL) || (substr == NULL)) {
        return NULL;
    }

    pstrret = (uint8_t *)strstr((char *)buf, substr);
    if (pstrret != NULL) {
        pstrret += strlen(substr);
	while (*pstrret == ' ' || *pstrret == '=') {
	    pstrret++;
	}

	return pstrret;
    }

    return NULL;
}

static void ConfigTrim(uint8_t *str)
{
    int32_t istrLen = (int32_t)strlen((char *)str);
    while (istrLen > 0) {
        if ((str[istrLen - 1] == '\n') || (str[istrLen - 1] == ' ') || (str[istrLen - 1] == '\r')) {
	    str[istrLen - 1] = '\0';
	    istrLen--;
	} else {
	    break;
	}
    }
}

static int32_t CheckIsOctal(uint8_t *ptemp)
{
    if ((*ptemp == '0') && (*(ptemp + 1) != 'x') && (*(ptemp + 1) != 'X') && (*(ptemp + 1) != '\0')) {
        return RETURN_OK;
    }

    return RETURN_ERROR;
}

static int32_t StringToIntByOctal(uint8_t *ptemp, int64_t *intValue)
{
    while (*ptemp != '\0') {
       if (*ptemp >= '0' && *ptemp <= '7') {
           *intValue = 8 * (*intValue) + *ptemp - '0';
	   ptemp++;
       } else {
           return RETURN_ERROR;
       }
    }

    return RETURN_OK;
}

static int32_t CheckIsHexadecimal(uint8_t *ptemp)
{
    if (((*ptemp == '0') && (*(ptemp + 1) == 'x')) || ((*ptemp == '0') && (*(ptemp + 1) == 'X'))) {
        return RETURN_OK;
    }

    return RETURN_ERROR;
}

static int32_t StringToIntByHexadecimal(uint8_t *ptemp, int64_t *intValue)
{
    while (*ptemp != '\0') {
        if (*ptemp >= 'a' && *ptemp <= 'f') {
	    *intValue = 16 * (*intValue) + *ptemp - 'a' + 10;
	} else if (*ptemp >= 'A' && *ptemp <= 'F') {
	    *intValue = 16 * (*intValue) + *ptemp - 'A' + 10;
	} else if (*ptemp >= '0' && *ptemp <= '9') {
	    *intValue = 16 * (*intValue) + *ptemp - '0';
	} else {
	    return RETURN_ERROR;
	}

	ptemp++;
    }

    return RETURN_OK;
}

static int32_t StringToIntByDecimal(uint8_t *ptemp, int64_t *intValue)
{
    while (*ptemp != '\0') {
	if (*ptemp >= '0' && *ptemp <= '9') {
	    *intValue = 10 * (*intValue) + *ptemp - '0';
	} else {
	    return RETURN_ERROR;
	}
	ptemp++;
    }

    return RETURN_OK;
}    

static int32_t TransformValueToInt(uint8_t *pvalue, uint64_t *resultValue)
{
    int64_t intValue = 0;
    uint8_t *ptemp = pvalue;
    int32_t ret;

    if (pvalue == NULL) {
	return RETURN_ERROR;
    }

    if (CheckIsOctal(ptemp) == RETURN_OK) {
	ptemp++;
	ret = StringToIntByOctal(ptemp, &intValue);
	if (ret != RETURN_OK) {
	    return RETURN_ERROR;
	}
    } else if (CheckIsHexadecimal(ptemp) == RETURN_OK) {
	ptemp = ptemp + 2;
	if (*ptemp == '\0') {
	    return RETURN_ERROR;
	}

	ret = StringToIntByHexadecimal(ptemp, &intValue);
	if (ret != RETURN_OK) {
	    return RETURN_ERROR;
	}
    } else {
	ret = StringToIntByDecimal(ptemp, &intValue);
	if (ret != RETURN_OK) {
	    return RETURN_ERROR;
	}
    }
    *resultValue = (uint64_t)intValue;
    return RETURN_OK;
}

static int32_t AnalyzeSubString(uint8_t *str)
{
    uint8_t *value;

    value = SearchSubString(str, ZK_SERVER_LIST);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value %s.", ZK_SERVER_LIST, value);
    if(strncpy(g_ProxyCfg.zkServerList, (char *)value, ZK_SERVER_LIST_STR_LEN) == NULL) {
        ProxyDbgLogErr("strncpy failed.");
        return RETURN_ERROR;
    }

	return RETURN_OK;
    }

    value = SearchSubString(str, CEPH_CONF_PATH);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo(">ead config: %s, value: %s", CEPH_CONF_PATH, value);
    if(strncpy(g_ProxyCfg.cephConfPath, (char *)value, MAX_PATH_LEN) == NULL) {
        ProxyDbgLogErr("strncpy failed.");
        return RETURN_ERROR;
    }
	return RETURN_OK;
    }

    value = SearchSubString(str, LOG_OUT_PATH);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s.", LOG_OUT_PATH, value);
    if(strncpy(g_ProxyCfg.logOutPath, (char *)value, MAX_PATH_LEN) == NULL) {
        ProxyDbgLogErr("strncpy failed.");
        return RETURN_ERROR;
    }
	return RETURN_OK;
    }

    value = SearchSubString(str, WORKER_NUM);
    if (value != NULL) {
        ConfigTrim(value);
        ProxyDbgLogInfo("read config: %s, value: %s.", WORKER_NUM, value);
        return TransformValueToInt(value, &g_ProxyCfg.workerNum);
    }

    value = SearchSubString(str, CORE_NUMBER);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s.", CORE_NUMBER, value);
    if(strncpy(g_ProxyCfg.coreIds, (char *)value, MAX_CORE_NUMBER) == NULL) {
        ProxyDbgLogErr("strncpy failed.");
        return RETURN_ERROR;
    }
	return RETURN_OK;
    }

    value = SearchSubString(str, MSGR_AMOUNT);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s.", MSGR_AMOUNT, value);
	return TransformValueToInt(value, &g_ProxyCfg.msgrAmount);
    }

    value = SearchSubString(str, BIND_CORE);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s.", BIND_CORE, value);
	return TransformValueToInt(value, &g_ProxyCfg.bindCore);
    }

    value = SearchSubString(str, RADOS_MON_OP_TIMEOUT);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s.", RADOS_MON_OP_TIMEOUT, value);
	return TransformValueToInt(value, &g_ProxyCfg.monTimeout);
    }

    value = SearchSubString(str, RADOS_OSD_OP_TIMEOUT);
    if (value != NULL) {
	ConfigTrim(value);
	ProxyDbgLogInfo("read config: %s, value: %s", RADOS_OSD_OP_TIMEOUT, value);
	return TransformValueToInt(value, &g_ProxyCfg.osdTimeout);
    }

    return RETURN_OK;
}

static int32_t ReadConfig(const char *configFile)
{
    FILE *pcfgFile = NULL;
    uint8_t acBuf[CONFIG_BUFFSIZE];
    int32_t ret = RETURN_OK;

    pcfgFile = fopen(configFile, "r");
    if (pcfgFile == NULL) {
	ProxyDbgLogErr("config failed to open file: %s.", configFile);
	return RETURN_ERROR;
    }

    while (ret != RETURN_ERROR) {
	memset(acBuf, 0, CONFIG_BUFFSIZE);
	ret = ConfigReadByLine(pcfgFile, acBuf, CONFIG_BUFFSIZE);
	if (ret != RETURN_OK) {
	    ProxyDbgLogInfo("config read over.");
	    ret = RETURN_OK;
	    break;
	}

	ret = AnalyzeSubString(acBuf);
	if (ret != RETURN_OK) {
	    ProxyDbgLogErr("config get config para failed(%s) str(%s).\n", configFile, (char *)acBuf);
	    break;
	}
    }
    fclose(pcfgFile);

    return ret;
}

int32_t ProxyConfigInit()
{
	int ret;
	memset(&g_ProxyCfg, 0, sizeof(ProxyControlCfg));
	InitClusterCfg();

	ret = ReadConfig(CLUSTER_CONFIG_FILE);
	if (ret != RETURN_OK) {
		ProxyDbgLogErr("read cluster config failed, ret %d.\n", ret);
		return ret;
	}

	return RETURN_OK;
}


char *ProxyGetZkServerList()
{
	return g_ProxyCfg.zkServerList;
}

char *ProxyGetCoreNumber()
{
	return g_ProxyCfg.coreIds;
}

char *ProxyGetCephConf() {
	return g_ProxyCfg.cephConfPath;
}

char *ProxyGetLogPath() {
	return g_ProxyCfg.logOutPath;
}

uint32_t ProxyGetWorkerNum()
{
	return g_ProxyCfg.workerNum;
}

uint32_t ProxyGetMsgrAmount()
{
	return g_ProxyCfg.msgrAmount;
}

uint32_t ProxyGetBindCore()
{
	return g_ProxyCfg.bindCore;
}

uint64_t ProxyGetOsdTimeOut()
{
	return g_ProxyCfg.osdTimeout;
}

uint64_t ProxyGetMonTimeOut()
{
	return g_ProxyCfg.monTimeout;
}

const char *ProxyGetOsdTimeOutOption()
{
	return RADOS_OSD_OP_TIMEOUT;
}

const char *ProxyGetMonTimeOutOption()
{
	return RADOS_MON_OP_TIMEOUT;
}
