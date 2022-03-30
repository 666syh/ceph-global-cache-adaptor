#include "CcmAdaptor.h"
#include "CephProxyLog.h"
#include <vector>

int ClusterManagerAdaptor::ReportCreatePool(std::vector<uint32_t> &pools)
{
    for (uint32_t i = 0; i < pools.size(); i++) {
        if (notifyCreateFunc != NULL) {
            int32_t ret = notifyCreateFunc(pools[i]);
            if (ret != 0) {
                ProxyDbgLogErr("notify pool[%u] create failed.", pools[i]);
                return -1;
            }
        }
    }

    return 0;
}

int ClusterManagerAdaptor::ReportDeletePool(std::vector<uint32_t> &pools)
{
    for (uint32_t i = 0; i < pools.size(); i++) {
        if (notifyDeleteFunc != NULL) {
            int32_t ret = notifyDeleteFunc(pools[i]);
            if (ret != 0) {
                ProxyDbgLogErr("notify pool[%u] delete failed.", pools[i]);
                return -1;
            }
        }
    }

    return 0;
}

int ClusterManagerAdaptor::RegisterPoolCreateReportFn(NotifyPoolEventFn fn)
{
    if (fn == NULL) {
        ProxyDbgLogErr("input argument is NULL");
        return -1;
    }

    if (notifyCreateFunc != NULL) {
        ProxyDbgLogErr("createFunc has already registered.");
        return -1;
    }

    notifyCreateFunc = fn;
    return 0;
}

int ClusterManagerAdaptor::RegisterPoolDeleteReportFn(NotifyPoolEventFn fn)
{
    if (fn == NULL) {
        ProxyDbgLogErr("input argument is invalid");
        return -1;
    }

    if (notifyDeleteFunc != NULL) {
        ProxyDbgLogErr("createFunc has already registered.");
        return -1;
    }

    notifyDeleteFunc = fn;
    return 0;
}