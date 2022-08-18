﻿/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
 *
 */

#include <string>
#include <map>
#include <vector>
#include <shared_mutex>
#include <atomic>
#include <assert.h>
#include <regex>
#include "rbd/librbd.h"
#include "rados/librados.h"
#include "rados/librados.hpp"
#include "rbd/librbd.hpp"
#include "CephExport.h"
#include "CephProxyLog.h"

#define DEFAULT_CEPH_CONF_PATH "/etc/ceph/ceph.conf"
#define RADOS_CONNECT_RETRY 5
#define CONNECT_WAIT_TIME 5
const uint32_t MB = (1024 * 1024);

class NoProgressContext : public librbd::ProgressContext {
public:
	NoProgressContext() {}
	int update_progress(uint64_t offset, uint64_t src_size) override
	{
		return 0;
	}
};

struct ProxyCtx {
	bool init_flag = false;
	librados::Rados client;
	std::shared_mutex lock;
	int ref;
};
struct SigPoolInfo {
	int64_t poolId;
	std::string poolName;
	bool isEC;
	int k;
	int m;
	uint64_t usage;
	std::string ecProfileName;
};

static struct ProxyCtx gProxyCtx;
using shared_lock = std::shared_lock<std::shared_mutex>;
using unique_lock = std::unique_lock<std::shared_mutex>;

static void GetPool(librados::IoCtx &io_ctx, int64_t &pool_id, std::string &pool_name)
{
	std::string poolName = io_ctx.get_pool_name();
	pool_id = io_ctx.get_id();
	pool_name.assign(poolName);
}

static void GetImage(librbd::Image &rbd_image, std::string &image_name, std::string &image_id)
{

	std::string imageName;
	std::string imageId;
	rbd_image.get_name(&imageName);
	rbd_image.get_id(&imageId);
	image_name.assign(imageName);
	image_id.assign(imageId);
}

static int ImageGetSnaps(librbd::Image& rbd_image,
	std::map<uint64_t, std::string>& snap_map_by_id,
	std::map<std::string, uint64_t>& snap_map_by_name,
	bool all = false)
{
	std::vector<librbd::snap_info_t> snaps;
	int ret;

	ret = rbd_image.snap_list(snaps);
	if (ret < 0) {
		ProxyDbgLogErr("rbd: unable to list snapshots, ret=%d", ret);
		return ret;
	}

	for (std::vector<librbd::snap_info_t>::iterator s = snaps.begin(); s != snaps.end(); s++) {
		if (!all) {
			librbd::snap_namespace_type_t namespace_type;
			int r = rbd_image.snap_get_namespace_type(s->id, &namespace_type);
			if (r < 0) {
				ProxyDbgLogErr("rbd: unable to get snap namespace type, snap=%lu:%s", s->id, s->name.c_str());
			} else if(namespace_type != RBD_SNAP_NAMESPACE_TYPE_USER){
				continue;
			}
		}
		snap_map_by_id[s->id] = s->name;
		snap_map_by_name[s->name] = s->id;
	}

	return ret;
}

static void RadosShutdown()
{
	unique_lock l(gProxyCtx.lock);
	gProxyCtx.ref--;
	if (gProxyCtx.ref == 0) {
		gProxyCtx.client.shutdown();
		gProxyCtx.init_flag = false;
	}
}

static void FastInitRados(std::map<std::string, std::string>& conf_map)
{
	assert(gProxyCtx.init_flag == true);
	gProxyCtx.ref++;
	std::map<std::string, std::string>::iterator iter;
	int ret;
	for (iter = conf_map.begin(); iter != conf_map.end(); iter++) {
		ret = gProxyCtx.client.conf_set(iter->first.c_str(), iter->second.c_str());
		if (ret < 0) {
			ProxyDbgLogErr("rados conf set %s=%s failed", iter->first.c_str(), iter->second.c_str());
			continue;
		}
		ProxyDbgLogDebug("rados conf set %s=%s", iter->first.c_str(), iter->second.c_str());
	}
}

static int RadosInit(const std::string& conf_path,
	std::map<std::string, std::string>& conf_map)
{
	uint32_t retryCount = 0;
	{
		shared_lock l(gProxyCtx.lock);
		if (gProxyCtx.init_flag) {
			FastInitRados(conf_map);
			return 0;
		}
	}

	unique_lock l(gProxyCtx.lock);
	if (gProxyCtx.init_flag) {
		FastInitRados(conf_map);
		return 0;
	}

	int ret = gProxyCtx.client.init(NULL);
	if (ret != 0) {
		ProxyDbgLogErr("rados create failed: %d", ret);
		return ret;
	}

	ret = gProxyCtx.client.conf_read_file(conf_path.c_str());
	if (ret < 0) {
		ProxyDbgLogErr("rados read file failed: %d", ret);
		gProxyCtx.client.shutdown();
		return ret;
	}

	std::map<std::string, std::string>::iterator iter;
	for (iter = conf_map.begin(); iter != conf_map.end(); iter++) {
		ret = gProxyCtx.client.conf_set(iter->first.c_str(), iter->second.c_str());
		if (ret < 0) {
			ProxyDbgLogErr("rados conf set %s=%s failed", iter->first.c_str(), iter->second.c_str());
			gProxyCtx.client.shutdown();
			return ret;
		}
		ProxyDbgLogDebug("rados conf set %s=%s", iter->first.c_str(), iter->second.c_str());
	}

	while (retryCount < RADOS_CONNECT_RETRY) {
		ret = gProxyCtx.client.connect();
		if (ret < 0) {
			ProxyDbgLogErr("connect ceph monitor failed: %d, retry:[%u/%d]", ret, retryCount + 1, RADOS_CONNECT_RETRY);
			retryCount++;
			sleep(CONNECT_WAIT_TIME * (1 << retryCount));
				continue;
		} else {
			break;
		}
	}

	if (ret < 0) {
		ProxyDbgLogErr("rados connect timeout failed: %d", ret);
		gProxyCtx.client.shutdown();
		return ret;
	}

	gProxyCtx.ref = 0;
	gProxyCtx.ref++;
	gProxyCtx.init_flag = true;

	ProxyDbgLogDebug("rados init success");
	return 0;
}

static void IoCtxDestroy(librados::IoCtx& ioctx)
{
	ioctx.close();
}

static int IoctxSetNamespace(librados::IoCtx& io_ctx, const std::string& namespace_name)
{
	if (!namespace_name.empty()) {
	librbd::RBD rbd;
	bool exists = false;
	int r = rbd.namespace_exists(io_ctx, namespace_name.c_str(), &exists);
	if (r < 0) {
		ProxyDbgLogErr("namespace_exists interface return %d", r);
		return r;
	}
	if (!exists) {
		ProxyDbgLogErr("name space %s not exists", namespace_name.c_str());
		return -ENOENT;
	}
	}
	io_ctx.set_namespace(namespace_name);
	return 0;
}

static int IoctxInit(librados::IoCtx* ioctx, const std::string& pool_name,
	int64_t pool_id, const std::string& namespace_name)
{
	int ret;
	assert(gProxyCtx.init_flag);
	if (!pool_name.empty()) {
		ret = gProxyCtx.client.ioctx_create(pool_name.c_str(), *ioctx);
		if (ret < 0) {
			ProxyDbgLogErr("Rados ioctx Init failed, pool_name %s, ret %d", pool_name.c_str(), ret);
			return ret;
		}
	} else {
		ret = gProxyCtx.client.ioctx_create2(pool_id, *ioctx);
		if (ret < 0) {
			ProxyDbgLogErr("Rados ioctx Init failed, pool_id %ld, ret %d", pool_id, ret);
			return ret;
		}
	}

	ret = IoctxSetNamespace(*ioctx, namespace_name);
	if (ret < 0) {
		ProxyDbgLogErr("rados set namespace failed, ns %s, ret %d", namespace_name.c_str(), ret);
		IoCtxDestroy(*ioctx);
		return ret;
	}

	return ret;
}

static void IoctxSetOsdmapFullTry(librados::IoCtx& io_ctx)
{
	io_ctx.set_osdmap_full_try();
}

static void IoctxCloseImage(librbd::Image& image)
{
	image.close();
}

static int IoctxOpenImage(librados::IoCtx &io_ctx, const std::string& image_name,
	const std::string& image_id, librbd::Image* image)
{
	int ret;
	librbd::RBD rbd;

	if (!image_id.empty()) {
		ret = rbd.open_by_id(io_ctx, *image, image_id.c_str());
		if (ret < 0) {
			ProxyDbgLogErr("rbd open image id %s failed %d", image_id.c_str(), ret);
			return ret;
		}
	} else {
		ret = rbd.open(io_ctx, *image, image_name.c_str());
		if (ret < 0) {
			ProxyDbgLogErr("rbd open image name %s failed %d", image_name.c_str(), ret);
			return ret;
		}
	}

	return ret;
}

static int ImageRemoveSnap(librbd::Image& image, const std::string& snap_name, uint64_t snap_id, bool force)
{
	int ret;

	if (!snap_name.empty()) {
	    uint32_t flags = force ? RBD_SNAP_REMOVE_FORCE : 0;
	    NoProgressContext prog_ctx;
	    ret = image.snap_remove2(snap_name.c_str(), flags, prog_ctx);
	    if (ret < 0) {
		    ProxyDbgLogErr("rbd snap remove failed. snap_name %s force %d ret %d",
			    snap_name.c_str(), force, ret);
		    return ret;
	    }
	} else {
		ret = image.snap_remove_by_id(snap_id);
		if (ret < 0) {
			ProxyDbgLogErr("rbd snap remove failed. snap_id %lu ret %d", snap_id, ret);
			return ret;
		}
	}

	return ret;
}

static int ImageCreateSnap(librbd::Image& image, const std::string& snap_name)
{
	int ret;

	ret = image.snap_create(snap_name.c_str());
	if (ret < 0) {
		ProxyDbgLogErr("rbd snap create failed. snap_name %s ret %d", snap_name.c_str(), ret);
	}
	return ret;
}

int CephLibrbdSnapRemove(int64_t pool_id,
	const char* _namespace_name,
	const char* _image_id,
	uint64_t snap_id,
	bool force)
{
	int ret;
	librados::IoCtx ioctx;
	librbd::Image image;
	std::string pool_name = "";
	std::string namespace_name = _namespace_name;
	std::string image_name = "";
	std::string image_id = _image_id;
	std::string snap_name = "";

	std::map<std::string, std::string> confMap;
	confMap["rbd_cache_writethrough_until_flush"] = "false";

	if (pool_id == 0 && pool_name.empty()) {
		ProxyDbgLogErr("both empty with pool id and pool name");
		return -EINVAL;
	} else if (image_id.empty() && image_name.empty()) {
		ProxyDbgLogErr("both empty with image id and image name");
		return -EINVAL;
	} else if (snap_id == 0 && snap_name.empty()) {
		ProxyDbgLogErr("both empty with snap id and snap name");
		return -EINVAL;
	}

	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

    ret = IoctxInit(&ioctx, pool_name, pool_id, namespace_name);
	if (ret < 0) {
		if (ret == -ENOENT) {
		    ret = 0;
		} else {
			ProxyDbgLogErr("ioctx init failed: %d", ret);
        }
		goto shutdown;
	}

	IoctxSetOsdmapFullTry(ioctx);

	ret = IoctxOpenImage(ioctx, image_name, image_id, &image);
	if (ret < 0) {
		if (ret == -ENOENT) {
			ret = 0;
		} else {
			ProxyDbgLogErr("image open failed: %d", ret);
		}
		goto close_ioctx;
	}

	ret = ImageRemoveSnap(image, snap_name, snap_id, force);
	if (ret < 0) {
		if (ret == -ENOENT) {
			ret = 0;
		} else {
			ProxyDbgLogErr("image remove snap failed: %d", ret);
		}
		goto close_image;
	}

	ProxyDbgLogDebug("image remove snap success pool %ld:%s image %s:%s snap %lu:%s",
		pool_id, pool_name, image_id, image_name, snap_id, snap_name);

close_image:
	IoctxCloseImage(image);

close_ioctx:
	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

int CephLibrbdSnapCreate(const char* _pool_name,
	int64_t pool_id,
	const char* _namespace_name,
	const char* _image_name,
	const char* _image_id,
	const char* _snap_name,
	uint64_t* snap_id)
{
	int ret;
	librados::IoCtx ioctx;
	librbd::Image image;
	std::string pool_name = _pool_name;
	std::string namespace_name = _namespace_name;
	std::string image_name = _image_name;
	std::string image_id = _image_id;
	std::string snap_name = _snap_name;
	std::map<uint64_t, std::string> snap_map_by_id;
	std::map<std::string, uint64_t> snap_map_by_name;

	if (pool_id == 0 && pool_name.empty()) {
		ProxyDbgLogErr("both empty with pool id and pool name");
		return -EINVAL;
	} else if (image_id.empty() && image_name.empty()) {
		ProxyDbgLogErr("both empty with image id and image name");
		return -EINVAL;
	} else if (snap_name.empty()) {
		ProxyDbgLogErr("empty with snap name");
		return -EINVAL;
	}

	std::map<std::string, std::string> confMap;
	confMap["rbd_cache_writethrough_until_flush"] = "false";

	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

	ret = IoctxInit(&ioctx, pool_name, pool_id, namespace_name);
	if (ret < 0) {
		ProxyDbgLogErr("ioctx Init failed: %d", ret);
		goto shutdown;
	}

	ret = IoctxOpenImage(ioctx, image_name, image_id, &image);
	if (ret < 0) {
		ProxyDbgLogErr("image open failed: %d", ret);
		goto close_ioctx;
	}

	ret = ImageCreateSnap(image, snap_name);
	if (ret < 0 && ret != -EEXIST) {
		ProxyDbgLogErr("image create snap failed: %d", ret);
		goto close_image;
	}

	ProxyDbgLogDebug("image create snap success pool %ld:%s image %s:%s snap %s",
		pool_id, pool_name, image_id, image_name, snap_name);

	ret = ImageGetSnaps(image, snap_map_by_id, snap_map_by_name);
	if (ret < 0) {
		ProxyDbgLogErr("image get snap failed: %d, should AGAIN", ret);
		ret = -EAGAIN;
		goto close_image;
	}

	assert(snap_map_by_name.count(snap_name) != 0);
	*snap_id = snap_map_by_name[snap_name];

close_image:
	IoctxCloseImage(image);

close_ioctx:
	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

int CephLibrbdSnapList(const char* _pool_name,
	int64_t pool_id,
	const char *_namespace_name,
	const char *_image_name,
	const char *_image_id,
	uint64_t **snap_map_id,
	char ***snap_map_name,
	int *snap_map_length)
{
	int ret;
	int i = 0;
	librados::IoCtx ioctx;
	librbd::Image image;
	std::string pool_name = _pool_name;
	std::string namespace_name = _namespace_name;
	std::string image_name = _image_name;
    std::string image_id = _image_id;
    std::map<uint64_t, std::string> snap_map_by_id;
    std::map<std::string, uint64_t> snap_map_by_name;

    if (pool_id == 0 && pool_name.empty()) {
	    ProxyDbgLogErr("both empty with pool id and pool name");
	    return -EINVAL;
    } else if (image_id.empty() && image_name.empty()) {
	    ProxyDbgLogErr("both empty with image id and image name");
	    return -EINVAL;
    } else if (*snap_map_id != NULL || *snap_map_name != NULL) {
	    ProxyDbgLogErr("snap_map_id or snap_map_name pointer params need NULL");
	    return -EINVAL;
}

    std::map<std::string, std::string> confMap;
    confMap["rbd_cache_writethrough_until_flush"] = "false";

    ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
    if (ret < 0) {
	    ProxyDbgLogErr("rados client Init failed: %d", ret);
	    return ret;
    }

    ret = IoctxInit(&ioctx, pool_name, 0, namespace_name);
    if (ret < 0) {
	    ProxyDbgLogErr("ioctx Init failed: %d", ret);
	    goto shutdown;
    }

    ret = IoctxOpenImage(ioctx, image_name, image_id, &image);
    if (ret < 0) {
	    ProxyDbgLogErr("image open failed: %d", ret);
	    goto close_ioctx;
    }

    ret = ImageGetSnaps(image, snap_map_by_id, snap_map_by_name);
    if (ret < 0) {
	    ProxyDbgLogErr("image get snaps failed: %d", ret);
	    goto close_image;
    }

    *snap_map_length = snap_map_by_id.size();
    if (*snap_map_length == 0) {
	    goto close_image;
    }
    // init
    *snap_map_id = (uint64_t*)calloc(*snap_map_length, sizeof(uint64_t));
    if (*snap_map_id == NULL) {
	    ret = -ENOMEM;
	    goto close_image;
    }
    *snap_map_name = (char **)calloc(*snap_map_length, sizeof(char*));
    if (*snap_map_name == NULL) {
	    ret = -ENOMEM;
	    free(*snap_map_id);
	    *snap_map_id = NULL;
	    goto close_image;
    }

    for (std::map<uint64_t, std::string>::iterator s = snap_map_by_id.begin(); s != snap_map_by_id.end(); s++) {
	    (*snap_map_id)[i] = s->first;
	    (*snap_map_name)[i] = (char*)calloc(1, s->second.length());
	    if ((*snap_map_name)[i] == NULL) {
		    ret = -ENOMEM;
		    break;
	    }
	    memcpy((*snap_map_name)[i], s->second.c_str(), s->second.length());
	    i++;
	    ProxyDbgLogDebug("image list snap (%d) pool %ld:%s image %s:%s snap %lu:%s", i,
		    pool_id, pool_name, image_id, image_name, s->first, s->second);
    }
    if (i < *snap_map_length) {
	    for (int j = 0; j < i; j++) {
		    free((*snap_map_name)[j]);
	    }
	    free(*snap_map_name);
	    free(*snap_map_id);
	    *snap_map_name = NULL;
	    *snap_map_id = NULL;
    }

close_image:
IoctxCloseImage(image);

close_ioctx:
IoCtxDestroy(ioctx);

shutdown:
RadosShutdown();
return ret;
}

void CephLibrbdSnapListEnd(uint64_t** snap_map_id,
	char*** snap_map_name,
	int snap_map_length)
{
	for (int i = 0; i < snap_map_length; i++) {
		free((*snap_map_name)[i]);
	}
	if (*snap_map_name) {
	    free(*snap_map_name);
		*snap_map_name = NULL;
	}
	if (*snap_map_id) {
	    free(*snap_map_id);
		*snap_map_id = NULL;
	}
}

int CephLibrbdGetPoolId(const std::string& pool_name, int64_t& pool_id,
	const std::string& namespace_name)
{
	int ret;
	librados::IoCtx ioctx;
    std::string pname;
	
	std::map<std::string, std::string> confMap;
	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

	ret = IoctxInit(&ioctx, pool_name, 0, namespace_name);
    if (ret < 0) {
		ProxyDbgLogErr("ioctx Init failed: %d", ret);
		goto shutdown;
	}

	GetPool(ioctx, pool_id, pname);
	assert(pname == pool_name);

	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

int CephLibrbdGetPoolName(std::string& pool_name, int64_t pool_id,
	const std::string& namespace_name)
{
	int ret;
	librados::IoCtx ioctx;
	int64_t pid;

	std::map<std::string, std::string> confMap;

	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

	ret = IoctxInit(&ioctx, "", pool_id, namespace_name);
	if (ret < 0) {
		ProxyDbgLogErr("ioctx Init failed: %d", ret);
		goto shutdown;
	}

	GetPool(ioctx, pid, pool_name);
	assert(pid == pool_id);

	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

int CephLibrbdGetImageId(const std::string& pool_name, int64_t pool_id,
	std::string &image_id, const std::string &image_name,
	const std::string& namespace_name)
{
	int ret;
	librados::IoCtx ioctx;
	librbd::Image image;
	std::string iname;

	std::map<std::string, std::string> confMap;
	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

	ret = IoctxInit(&ioctx, pool_name, pool_id, namespace_name);
	if (ret < 0) {
		ProxyDbgLogErr("ioctx Init failed: %d", ret);
		goto shutdown;
	}

	ret = IoctxOpenImage(ioctx, image_name, "", &image);
	if (ret < 0) {
		ProxyDbgLogErr("image open failed: %d", ret);
		goto close_ioctx;
	}

	GetImage(image, iname, image_id);
	assert(iname == image_name);

	IoctxCloseImage(image);

close_ioctx:
	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

int CephLibrbdGetImageName(const std::string& pool_name, int64_t pool_id,
	const std::string& image_id, std::string& image_name,
	const std::string& namespace_name)
{
	int ret;
	librados::IoCtx ioctx;
	librbd::Image image;
	std::string iid;

	std::map<std::string, std::string> confMap;
	ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
	if (ret < 0) {
		ProxyDbgLogErr("rados client Init failed: %d", ret);
		return ret;
	}

	ret = IoctxInit(&ioctx, "", pool_id, namespace_name);
	if (ret < 0) {
		ProxyDbgLogErr("ioctx Init failed: %d", ret);
		goto shutdown;
	}

	ret = IoctxOpenImage(ioctx, "", image_id, &image);
	if (ret < 0) {
		ProxyDbgLogErr("image open failed: %d", ret);
		goto close_ioctx;
	}

	GetImage(image, image_name, iid);
	assert(iid == image_id);

	IoctxCloseImage(image);

close_ioctx:
	IoCtxDestroy(ioctx);

shutdown:
	RadosShutdown();
	return ret;
}

static int MonCommand(std::string cmd, std::string &_outs)
{
	int ret;
	assert(gProxyCtx.init_flag);
	std::string outs;
	bufferlist inbl;
	bufferlist outbl;
	ret = gProxyCtx.client.mon_command(cmd, inbl, &outbl, &outs);
	if (ret < 0) {
		ProxyDbgLogWarn("mon_command failed: %s, ret=%d", cmd.c_str(), ret);
		return ret;
	}
	_outs = outbl.to_str().c_str();
	return 0;
}

static int ParsePoolMap(std::string &outs, std::map<int64_t, struct SigPoolInfo> &poolMap)
{
	std::string pattern = "\\s*(\\d+)\\s+(\\S+)";
	std::vector<std::string> preVec;
	char *strs = new char[outs.length() + 1];
	if (strs == nullptr) {
		ProxyDbgLogErr("alloc memory failed");
		return -1;
	}
	strcpy(strs, outs.c_str());
	char *p = strtok(strs, "\n");
	while (p) {
		std::string s = p;
		preVec.push_back(s);
		p = strtok(NULL, "\n");
	}

	for (size_t i = 0; i < preVec.size(); i++) {
		std::regex expression(pattern);
		std::smatch result;
		bool flag = std::regex_match(preVec[i], result, expression);
		if (flag) {
			int64_t poolId = atoi(result[1].str().c_str());
			struct SigPoolInfo info;
			info.poolId = poolId;
			info.poolName = result[2].str().c_str();
			info.usage = 0;
			poolMap[poolId] = info;
			ProxyDbgLogInfo("pool %s, id %ld", info.poolName.c_str(), info.poolId);
		}
	}

	if (strs) {
		delete[] strs;
		strs = nullptr;
	}
	return 0;
}

static int GetECPoolScale(struct SigPoolInfo &info)
{
	std::string cmd("{\"prefix\": \"osd erasure-code-profile get\", \"name\": \"");
	std::string outs;
	cmd.append(info.ecProfileName);
	cmd.append(std::string("\"}"));

	int ret = MonCommand(cmd, outs);
	if (ret != 0) {
		ProxyDbgLogErr("Get erasure code profile failed, profile=%s, ret=%d", info.ecProfileName.c_str(), ret);
		return ret;
	}

	std::vector<std::string> preVec;
	char *strs = new char[outs.length() + 1];
	if (strs == nullptr) {
		ProxyDbgLogErr("alloc memory failed");
		return -1;
	}
	strcpy(strs, outs.c_str());
	char *p = strtok(strs, "\n");
	while (p) {
		std::string s = p;
		preVec.push_back(s);
		p = strtok(NULL, "\n");
	}

	for (uint32_t i = 0; i < preVec.size(); i++) {
		std::regex expression("(\\w+)=(\\w+)");
		std::smatch result;
		bool flag = std::regex_match(preVec[i], result, expression);
		if (!flag) {
			continue;
		}
		if (result[1].str().compare("k") == 0) {
			info.k = atoi(result[2].str().c_str());
		} else if (result[1].str().compare("m") == 0) {
			info.m = atoi(result[2].str().c_str());
		}
	}

	if (strs) {
		delete[] strs;
		strs = nullptr;
	}
	return 0;
}

static int CalPoolProperty(std::map<int64_t, struct SigPoolInfo> &poolMap)
{
	for (std::map<int64_t, struct SigPoolInfo>::iterator p = poolMap.begin(); p != poolMap.end(); p++) {
		struct SigPoolInfo& info = p->second;
		std::string cmd("{\"var\": \"erasure_code_profile\", \"prefix\": \"osd pool get\", \"pool\": \"");
		std::string outs;
		cmd.append(info.poolName);
		cmd.append(std::string("\"}"));
		int ret = MonCommand(cmd, outs);
		if (ret < 0) {
			if (ret == -EACCES) {
				info.isEC = false;
				info.k = 1;
				info.m = 2;
				ProxyDbgLogInfo("Non EC pool %s, k=%d, m=%d", info.poolName.c_str(), info.k, info.m);
			} else {
				ProxyDbgLogErr("Get erasure code profile failed, name=%s, ret=%d", info.poolName.c_str(), ret);
				return ret;
			}
		} else {
			info.isEC = true;
			info.k = info.m = 0;
			std::regex expression("erasure_code_profile:\\s(\\S+)\\n?");
			std::smatch result;
			std::string outStr(outs.c_str());
			bool flag = std::regex_match(outStr, result, expression);
			if (flag) {
				info.ecProfileName = result[1].str().c_str();
			} else {
				ProxyDbgLogErr("regex erasure code profile failed, name=%s, out=%s",
					info.poolName.c_str(), outs.c_str());
				return -1;
			}
			ret = GetECPoolScale(info);
			if (ret < 0) {
				ProxyDbgLogErr("get EC pool size failed! ret=%d", ret);
				return ret;
			}
			assert(info.k != 0 && info.m != 0);
			ProxyDbgLogInfo("EC pool %s, k=%d, m=%d", info.poolName.c_str(), info.k, info.m);
		}
	}
	return 0;
}

static int DiffCallback(uint64_t offset, size_t len, int exists, void *arg)
{
	uint64_t *used = reinterpret_cast<uint64_t *>(arg);
	if (exists) {
		(*used) += len;
	}
	return 0;
}

static int CalImageUsage(librados::IoCtx &ioctx, struct SigPoolInfo& info, librbd::image_spec_t& imageSpec)
{
	int ret;
	librbd::RBD rbd;
	librbd::Image image;
	bool exact = false;
	ret = rbd.open_read_only(ioctx, image, imageSpec.name.c_str(), NULL);
	if (ret < 0) {
		ProxyDbgLogErr("image open failed, image=%s/%s, ret=%d", 
			info.poolName.c_str(), imageSpec.name.c_str(), ret);
		return ret;
	}

	librbd::image_info_t state;
	std::string lastSnap;
	std::vector<librbd::snap_info_t> snapList;
	uint64_t used;
	const char *snapFrom = nullptr;

	ret = image.stat(state, sizeof(state));
	if (ret < 0) {
		ProxyDbgLogErr("image stat failed, image=%s/%s, ret=%d", 
			info.poolName.c_str(), imageSpec.name.c_str(), ret);
		goto image_close;
	}

	ret = image.snap_list(snapList);
	if (ret < 0) {
		ProxyDbgLogErr("image snap list failed, image=%s/%s, ret=%d", 
			info.poolName.c_str(), imageSpec.name.c_str(), ret);
		goto image_close;
	}

	for (librbd::snap_info_t& snap : snapList) {
		librbd::Image snap_image;
		ret = rbd.open_read_only(ioctx, snap_image, imageSpec.name.c_str(), snap.name.c_str());
		if (ret < 0) {
			ProxyDbgLogErr("snap open failed, image=%s/%s@%s, ret=%d", 
				info.poolName.c_str(), imageSpec.name.c_str(), snap.name.c_str(), ret);
			goto image_close;
		}

		if (!lastSnap.empty()) {
			snapFrom = lastSnap.c_str();
		}
		used = 0;
		ret = snap_image.diff_iterate2(snapFrom, 0, snap.size, false, !exact, &DiffCallback, &used);
		if (ret < 0) {
			ProxyDbgLogErr("snap diff failed, image=%s/%s@%s, ret=%d", 
				info.poolName.c_str(), imageSpec.name.c_str(), snap.name.c_str(), ret);
			snap_image.close();
			goto image_close;
		}
		info.usage += used;
		lastSnap = snap.name.c_str();
		ProxyDbgLogInfo("snap %s/%s@%s, used=%lu",
			info.poolName.c_str(), imageSpec.name.c_str(), snap.name.c_str(), used);
	}

	if (!lastSnap.empty()) {
		snapFrom = lastSnap.c_str();
	}
	used = 0;
	ret = image.diff_iterate2(snapFrom, 0, state.size, false, !exact, &DiffCallback, &used);
	if (ret < 0) {
		ProxyDbgLogErr("image diff failed, image=%s/%s, ret=%d", 
			info.poolName.c_str(), imageSpec.name.c_str(), ret);
		goto image_close;
	}
	info.usage += used;
	ProxyDbgLogInfo("image %s/%s, used=%lu", info.poolName.c_str(), imageSpec.name.c_str(), used);
image_close:
	image.close();
	return ret;
}

static int CalPoolUsage(std::map<int64_t, struct SigPoolInfo> &poolMap)
{
	int ret = 0;
	for (std::map<int64_t, struct SigPoolInfo>::iterator p = poolMap.begin(); p!= poolMap.end(); p++) {
		struct SigPoolInfo& info = p->second;
		librados::IoCtx ioctx;
		ret = IoctxInit(&ioctx, info.poolName, 0, "");
		if (ret < 0) {
			ProxyDbgLogErr("ioctx Init failed, poolName=%s, ret=%d", info.poolName.c_str(), ret);
			return ret;
		}
		librbd::RBD rbd;
		std::vector<librbd::image_spec_t> images;
		ret = rbd.list2(ioctx, &images);
		if (ret < 0) {
			ProxyDbgLogErr("ioctx list image failed, poolName=%s, ret=%d", info.poolName.c_str(), ret);
			IoCtxDestroy(ioctx);
			return ret;
		}
		for (librbd::image_spec_t& imageSpec: images) {
			ret = CalImageUsage(ioctx, info, imageSpec);
			if (ret < 0) {
				ProxyDbgLogErr("cal image usage failed, image=%s/%s, ret=%d",
					info.poolName.c_str(), imageSpec.name.c_str(), ret);
				IoCtxDestroy(ioctx);
				return ret;
			}
		}
		IoCtxDestroy(ioctx);
	}
	return ret;
}

int CephLibrbdDiskUsage(uint64_t *usage)
{
	if (usage == nullptr) {
		return -1;
	}

	std::map<std::string, std::string> confMap;
    confMap["rbd_cache_writethrough_until_flush"] = "false";
	std::map<int64_t, struct SigPoolInfo> poolMap;
	std::map<int64_t, struct SigPoolInfo>::iterator p;

    int ret = RadosInit(std::string(DEFAULT_CEPH_CONF_PATH), confMap);
    if (ret < 0) {
	    ProxyDbgLogErr("rados client Init failed: %d", ret);
	    return ret;
    }
	std::string cmd("{\"prefix\":\"osd lspools\"}");
	std::string outs;
	ret = MonCommand(cmd, outs);
	if (ret < 0) {
		ProxyDbgLogErr("lspools failed: %d", ret);
	    goto shutdown;
	}

	ret = ParsePoolMap(outs, poolMap);
	if (ret < 0) {
		goto shutdown;
	}

	ret = CalPoolProperty(poolMap);
	if (ret < 0) {
		goto shutdown;
	}

	ret = CalPoolUsage(poolMap);
	if (ret < 0) {
		goto shutdown;
	}

	*usage = 0;
	for (p = poolMap.begin(); p != poolMap.end(); p++) {
		struct SigPoolInfo& info = p->second;
		*usage += info.usage * (info.k + info.m) / info.k;
	}
	*usage /= MB;
shutdown:
	RadosShutdown();
	return ret;
}