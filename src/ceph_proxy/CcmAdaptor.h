#ifndef CLUSTER_MGR_ADAPTOR_H
#define CLUSTER_MGR_ADAPTOR_H

#include <vector>
#include "CephProxyInterface.h"

class ClusterManagerAdaptor {
private:
    NotifyPoolEventFn notifyCreateFunc;
    NotifyPoolEventFn notifyDeleteFunc;
public:
    ClusterManagerAdaptor():notifyCreateFunc(NULL),
                            notifyDeleteFunc(NULL) { }
    ~ClusterManagerAdaptor() {}

    int ReportCreatePool(std::vector<uint32_t> &pools);
    int ReportDeletePool(std::vector<uint32_t> &pools);

    int RegisterPoolCreateReportFn(NotifyPoolEventFn fn);
    int RegisterPoolDeleteReportFn(NotifyPoolEventFn fn);
};

#endif