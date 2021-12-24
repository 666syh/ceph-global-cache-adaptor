















#ifndef SA_LOG_H
#define SA_LOG_H

#include <climits>
#include <memory>
#include <string>
#include <algorithm>
#include <iostream>

#include "sa_def.h"
#include "sa_export.h"

enum LogLevel {
    LV_CRITICAL = 0,
    LV_ERROR = 1,
    LV_WARNING = 2,
    LV_INFORMATION = 3,
    LV_DEBUG = 4
};

int InitSalog(SaExport &sa);

int FinishSalog(const std::string &name);

void OsaWriteLog(const int log_level, std::string file_name, int f_line, const std::string func_name,
		const char *format, ...);
void OsaWriteLogLimit(const int log_level, std::string file_name, int f_line, const std::string func_name,
		const char *format, ...);
void OsaWriteLogLimit2(const int log_level, std::string file_name, int f_line, const std::string func_name,
		const char *format, ...);

#define Salog(level, subModule, format, ...)      \
   do {						  \
	  OsaWriteLog(level, __FILE__, __LINE__, __func__, format, ## __VA_ARGS__);\
      } while (0) 

#define SalogLimit(level, subModule, format, ...)      \
   do {						  \
	  OsaWriteLogLimit(level, __FILE__, __LINE__, __func__, format, ## __VA_ARGS__);\
      } while (0) 

#define SalogLimit2(level, subModule, format, ...)      \
   do {						  \
	  OsaWriteLogLimit2(level, __FILE__, __LINE__, __func__, format, ## __VA_ARGS__);\
      } while (0) 

#endif








