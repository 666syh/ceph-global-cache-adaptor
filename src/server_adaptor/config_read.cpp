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

#include "config_read.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>



#define CLUSTER_CONFIG_FILE "/home/config_sa.conf"
#define CONFIG_BUFFSIZE 500
#define MAX_CPU_NUM (256)

static const char *LOCAL_IPV4_ADDR = "local_ipv4_addr";
static const char *LOCAL_PORT = "local_port";
static const char *ZK_SERVER_LIST = "zk_server_list";
static const char *IOD_CORE = "iod_core";
static const char *XNET_CORE = "xnet_core";
static const char *DPSHM_CORE = "dpshm_core";
static const char *GVA_SLAB_OBJ_NUM = "gva_slab_obj_num";
static const char *USE_ONE_SIDE_RMDA = "use_one_side_rdma";

static const char *LISTEN_IP = "listen_ip";
static const char *LISTEN_PORT = "listen_port";
static const char *SEND_IP = "send_ip";
static const char *SEND_PORT = "send_port";
static const char *TEST_MODE = "test_mode";
static const char *CORE_NUMBER = "core_number";
static const char *QUEUE_AMOUNT = "queue_amount";
static const char *QUEUE_MAX_CAPACITY = "queue_max_capacity";
static const char *MSGR_AMOUNT = "msgr_amount";
static const char *BIND_CORE = "bind_core";
static const char *BIND_QUEUE_CORE = "bind_queue_core";
static const char *PERF = "perf";

#ifndef RETURN_OK
#define RETURN_OK 0
#endif

#ifndef RETURN_ERROR
#define RETURN_ERROR (-1)
#endif

typedef struct {
	uint64_t ipv4Addr { 0 };
	uint64_t port { 0 };
	char ipv4AddrStr[MAX_IPV4_ADDR_LEN];
	char zkServerList[ZK_SERVER_LIST_STR_LEN];

	uint32_t iodCore[MAX_IOD_CORE];
	uint32_t xnetCore[MAX_XNET_CORE];
	uint32_t dpshmCore[MAX_DPSHM_CORE];

	uint64_t gvaSlabObjNum { 0 };
	uint64_t isUserOneSideRDMA { 0 };

	char listenIp[MAX_IPV4_ADDR_LEN];
	char listenPort[MAX_PORT_SIZE];
	char sendIp[MAX_IPV4_ADDR_LEN];
	char sendPort[MAX_PORT_SIZE];
	char testMode[MAX_PORT_SIZE];
	char coreNumber[MAX_PORT_SIZE];
	uint64_t queueAmount { 0 };
	uint64_t msgrAmount { 0 };
	uint64_t bindCore { 0 };
	uint64_t bindQueueCore { 0 };
	uint64_t perf { 0 };
	uint64_t queueMaxCapacity { 0 };
} SA_ClusterControlCfg;

SA_ClusterControlCfg g_SaClusterControlCfg = { 0 };

static void InitClusterCfg()
{
	int32_t i;
	for (i = 0; i < MAX_IOD_CORE; i++) {
		g_SaClusterControlCfg.iodCore[i] = UINT32_MAX;
	}

	for (i = 0; i < MAX_XNET_CORE; i++) {
		g_SaClusterControlCfg.xnetCore[i] = UINT32_MAX;
	}

	for (i =0; i < MAX_DPSHM_CORE; i++) {
		g_SaClusterControlCfg.dpshmCore[i] = UINT32_MAX;
	}

	g_SaClusterControlCfg.isUserOneSideRDMA = false;
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
			ptemp ++;
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
		} else if (*ptemp >= '0' && *ptemp <='9') {
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
		if (ret != RETURN_OK){
			return RETURN_ERROR;
		}
	}

	*resultValue = (uint64_t)intValue;
	return RETURN_OK;
}

static int32_t ParseCoreConfig(char *str, uint32_t *cores, uint8_t maxCoreNum)
{
	char *ctx = NULL;
	char *item = NULL;

	int idx = 0;
	for (item = strtok_r(str, ",",&ctx); item != NULL && idx < maxCoreNum; item = strtok_r(NULL, ",",&ctx), idx++) {
		uint64_t coreid = UINT64_MAX;
		TransformValueToInt((uint8_t *)item, &coreid);
		if (coreid > UINT8_MAX) {

			return RETURN_ERROR;
		}
		cores[idx] = coreid;
	}
	
	return RETURN_OK;
}

static int32_t AnalyzeSubString(uint8_t *str)
{
	uint8_t *value;

	value = SearchSubString(str, LOCAL_IPV4_ADDR);
	if (value != NULL) {
		ConfigTrim(value);



		strcpy(g_SaClusterControlCfg.ipv4AddrStr, (char *)value);

		return RETURN_OK;
	}

	value = SearchSubString(str, LOCAL_PORT);
	if (value != NULL) {
		ConfigTrim(value);


		return TransformValueToInt(value, &g_SaClusterControlCfg.port);
	}

	value = SearchSubString(str, ZK_SERVER_LIST);
	if (value != NULL) {
		ConfigTrim(value);

		strcpy(g_SaClusterControlCfg.zkServerList, (char *)value);

		return RETURN_OK;
	}

	value = SearchSubString(str, IOD_CORE);
	if (value != NULL) {
		ConfigTrim(value);

		return ParseCoreConfig((char *)value, g_SaClusterControlCfg.iodCore, MAX_IOD_CORE);
	}

	value = SearchSubString(str, XNET_CORE);
	if (value != NULL) {
		ConfigTrim(value);

                return ParseCoreConfig((char *)value, g_SaClusterControlCfg.xnetCore, MAX_XNET_CORE);
	}

	value = SearchSubString(str, DPSHM_CORE);
	if (value != NULL) {
		ConfigTrim(value);

		return ParseCoreConfig((char *)value, g_SaClusterControlCfg.dpshmCore, MAX_DPSHM_CORE);
	}
	
	value = SearchSubString(str, GVA_SLAB_OBJ_NUM);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.gvaSlabObjNum);
	}

	value = SearchSubString(str, USE_ONE_SIDE_RMDA);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.isUserOneSideRDMA);
	}
	value = SearchSubString(str, LISTEN_IP);
	if (value != NULL) {
		ConfigTrim(value);

		if (strlen((char *)value) >= MAX_IPV4_ADDR_LEN) {
			return RETURN_ERROR;
		}
		strcpy(g_SaClusterControlCfg.listenIp, (char *)value);
		return RETURN_OK;
	}
	value = SearchSubString(str, LISTEN_PORT);
	if (value != NULL) {
		ConfigTrim(value);

		if (strlen((char *)value) >= MAX_PORT_SIZE) {
			return RETURN_ERROR;
		}
		strcpy(g_SaClusterControlCfg.listenPort, (char *)value);
		return RETURN_OK;
	}
	value = SearchSubString(str, SEND_IP);
	if (value != NULL) {
		ConfigTrim(value);

		if (strlen((char *)value) >= MAX_IPV4_ADDR_LEN) {
			return RETURN_ERROR;
		}
		strcpy(g_SaClusterControlCfg.sendIp, (char *)value);
		return RETURN_OK;
	}
	value = SearchSubString(str, SEND_PORT);
	if (value != NULL) {
		ConfigTrim(value);

		if (strlen((char *)value) >= MAX_PORT_SIZE) {
			return RETURN_ERROR;
		}
		strcpy(g_SaClusterControlCfg.sendPort, (char *)value);
		return RETURN_OK;
	}
	value = SearchSubString(str, TEST_MODE);
	if (value != NULL) {
		ConfigTrim(value);

		 if (strlen((char *)value) >= MAX_PORT_SIZE) {
			 return RETURN_ERROR;
		 }
		 strcpy(g_SaClusterControlCfg.testMode, (char *)value);
		 return RETURN_OK;
	}       
	value = SearchSubString(str, CORE_NUMBER);
	if (value != NULL) {
		ConfigTrim(value);

		 if (strlen((char *)value) >= MAX_PORT_SIZE) {
			 return RETURN_ERROR;
		 }
		 strcpy(g_SaClusterControlCfg.coreNumber, (char *)value);
		 return RETURN_OK;

	}       
	value = SearchSubString(str, QUEUE_AMOUNT);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.queueAmount);
	}
	value = SearchSubString(str, QUEUE_MAX_CAPACITY);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.queueMaxCapacity);
	}
	value = SearchSubString(str, MSGR_AMOUNT);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.msgrAmount);
	}
	value = SearchSubString(str, BIND_CORE);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.bindCore);
	}
	value = SearchSubString(str, BIND_QUEUE_CORE);
	if (value != NULL) {
		ConfigTrim(value);

		return TransformValueToInt(value, &g_SaClusterControlCfg.bindQueueCore);
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

		fprintf(stderr, "config failed to open file: %s.", configFile);
		return RETURN_ERROR;
	}

	while (ret != RETURN_ERROR) {
		memset(acBuf, 0, CONFIG_BUFFSIZE);
		ret = ConfigReadByLine(pcfgFile, acBuf, CONFIG_BUFFSIZE);
		if (ret != RETURN_OK) {


			ret = RETURN_OK;
			break;
		}


		ret = AnalyzeSubString(acBuf);
		if (ret != RETURN_OK) {

			fprintf(stderr,"config get config para failed(%s) str(%s).\n", configFile, (char *)acBuf);
			break;
		}
	}
	fclose(pcfgFile);

	return ret;
}

int32_t OsaConfigRead::CacheClusterConfigInit(const char *filepath)
{
	int ret;

	memset(&g_SaClusterControlCfg, 0, sizeof(SA_ClusterControlCfg));
	InitClusterCfg();

	ret = ReadConfig(filepath);
	if (ret != RETURN_OK) {

		fprintf(stderr, "read cluster config failed, ret %d.\n",ret);
		return ret;
	}


	return RETURN_OK;
}

uint32_t OsaConfigRead::GetLocalIpv4Addr()
{
	return g_SaClusterControlCfg.ipv4Addr;
}

uint32_t OsaConfigRead::GetLocalPort()
{
	return g_SaClusterControlCfg.port;
}

uint32_t OsaConfigRead::IsUseOneSideRDMA()
{
	return g_SaClusterControlCfg.isUserOneSideRDMA;
}

uint32_t OsaConfigRead::GetGvaSlabObjNum()
{
	return g_SaClusterControlCfg.gvaSlabObjNum;
}

char *OsaConfigRead::GetLocalIpv4AddrStr()
{
	return g_SaClusterControlCfg.ipv4AddrStr;
}

char *OsaConfigRead::GetZkServerList()
{
	return g_SaClusterControlCfg.zkServerList;
}

char *OsaConfigRead::GetListenIp()
{
	return g_SaClusterControlCfg.listenIp;
}
char *OsaConfigRead::GetListenPort()
{
	return g_SaClusterControlCfg.listenPort;
}
char *OsaConfigRead::GetSendIp()
{
	return g_SaClusterControlCfg.sendIp;
}
char *OsaConfigRead::GetSendPort()
{
	return g_SaClusterControlCfg.sendPort;
}
char *OsaConfigRead::GetTestMode()
{
	return g_SaClusterControlCfg.testMode;
}
char *OsaConfigRead::GetCoreNumber()
{
	return g_SaClusterControlCfg.coreNumber;
}
uint32_t OsaConfigRead::GetQueueAmount()
{
	return g_SaClusterControlCfg.queueAmount;
}

uint32_t OsaConfigRead::GetMsgrAmount()
{
	return g_SaClusterControlCfg.msgrAmount;
}
uint32_t  OsaConfigRead::GetBindCore()
{
	return g_SaClusterControlCfg.bindCore;
}

uint32_t OsaConfigRead::GetBindQueueCore()
{
	return g_SaClusterControlCfg.bindQueueCore;
}

uint32_t OsaConfigRead::GetPerf()
{
	return g_SaClusterControlCfg.perf;
}
uint32_t OsaConfigRead::GetQueueMaxCapacity()
{
	return g_SaClusterControlCfg.queueMaxCapacity;
}

static int32_t CopyCores(uint32_t *destCores, uint32_t maxCoreNum, uint32_t *srcCores, uint32_t maxSrcCoreNum)
{
	if (destCores == NULL || srcCores == NULL) {
		return 0;
	}

	uint32_t idx = 0;
	for(idx = 0; idx < maxCoreNum && idx < maxSrcCoreNum; idx++) {
		if (srcCores[idx] < MAX_CPU_NUM) {
			destCores[idx] = srcCores[idx];
		} else {
			break;
		}
	}
}

int32_t OsaConfigRead::GetIodCore(uint32_t *cores, uint32_t maxCoreNum)
{
	return CopyCores(cores, maxCoreNum, g_SaClusterControlCfg.iodCore, MAX_IOD_CORE);
}

int32_t OsaConfigRead::GetXnetCore(uint32_t *cores, uint32_t maxCoreNum)
{
	return CopyCores(cores, maxCoreNum, g_SaClusterControlCfg.xnetCore, MAX_XNET_CORE);
}

int32_t OsaConfigRead::GetDpshmCore(uint32_t *cores, uint32_t maxCoreNum)
{
	return CopyCores(cores, maxCoreNum, g_SaClusterControlCfg.dpshmCore, MAX_DPSHM_CORE);
}
