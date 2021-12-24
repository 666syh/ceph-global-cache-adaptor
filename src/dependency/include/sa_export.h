/*
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */

#ifndef SA_EXPORT_H
#define SA_EXPORT_H

#include "sa_def.h"

class OphandlerModule;
class SaExport {
    std::string confPath;

public :
    //
    void Init(OphandlerModule &p);

    void DoOneOps(SaOpReq &saOp);

    void WriteLog(const int logLevel, const std::string &fileName, const int fLine, const std::string &funcName,
        const std::string &format);
    void WriteLogLimit(const int logLevel, const std::string &fileName, const int fLine, const std::string &funcName,
	const std::string &format);
    void WriteLogLimit2(const int logLevel, const std::string &fileName, const int fLine, const std::string &funcName,
        const std::string &format);

    void SetConfPath(const std::string &path);
    std::string GetConfPath();

    void FtdsStartNormal(unsigned int id, uint64_t ts);
    void FtdsEndNormal(unsigned int id, uint64_t ts, int ret);
    void FtdsStartHigh(unsigned int id, uint64_t ts);
    void FtdsEndHigt(unsigned int id, uint64_t ts, int ret);
};

#endif

