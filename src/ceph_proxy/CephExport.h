/* License:LGPL-2.1
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
 *
 */

#ifndef _CEPH_EXPORT_H_
#define _CEPH_EXPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

int CephLibrbdSnapRemove(int64_t pool_id,
		const char* namespace_name,
		const char* image_id,
		uint64_t snap_id,
		bool force);

int CephLibrbdSnapCreate(const char* pool_name,
	int64_t pool_id,
	const char* namespace_name,
	const char* image_name,
	const char* image_id,
	const char* snap_name,
	uint64_t* snap_id);

int CephLibrbdSnapList(const char* _pool_name,
	int64_t pool_id,
	const char* _namespace_name,
	const char* _image_name,
	const char* _image_id,
	uint64_t** snap_map_id,
	char*** snap_map_name,
	int* snap_map_length);


void CephLibrbdSnapListEnd(uint64_t** snap_map_id,
	char*** snap_map_name,
	int snap_map_length);

int CephLibrbdGetPoolId(const std::string& pool_name, int64_t& pool_id,
	const std::string& namespace_name);


int CephLibrbdGetPoolName(std::string& pool_name, int64_t pool_id,
	const std::string& namespace_name);


int CephLibrbdGetImageId(const std::string& pool_name, int64_t pool_id,
	std::string& image_id, const std::string& image_name,
	const std::string& namespace_name);


int CephLibrbdGetImageName(const std::string& pool_name, int64_t pool_id,
	const std::string& image_id, std::string& image_name,
	const std::string& namespace_name);

#ifdef __cplusplus
}
#endif

#endif //_LIBRBD_EXPORT_H_