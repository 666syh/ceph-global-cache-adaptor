#ifndef _CONFIG_READ_H_
#define _CONFIG_READ_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_IPV$_ADDR_LEN 16
#define MAX_IOD_CORE 8
#define MAX_PORT_SIZE 10
#define MAX_XNET_CORE 4
#define MAX_DPSHM_CORE 8
#define ZK_SERCER_LIST_STR_LEN 128
#define MAX_PATH_LEN 128
#define MAX_CORE_NUMBER 128


int32_t ProxyConfigInit();

char *ProxyGetZkServerList();
char *ProxyGetCoreNumber();
char *ProxyGetCephConf();
char *ProxyGetLogPath();
uint32_t ProxyGetWorkerNum();
uint32_t ProxyGetMsgrAmount();
uint32_t ProxyGetBindCore();

const char* ProxyGetOsdTimeOutOption();
uint64_t ProxyGetOsdTimeOut();
const char *ProxyGetMonTimeOutOption();
uint64_t ProxyGetMonTimeOut();

#endif

