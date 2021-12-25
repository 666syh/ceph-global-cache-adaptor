
set(lib_rados_include_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ceph/src/include)

set(lib_dpax_include_dir ${GLOBAL_CACHE_DIR}/src/include/infras/infrastructure/osax)
set(lib_lvos_include_idr ${GLOBAL_CACHE_DIR}/src/include/infras/infrastructure/lvos)
set(lib_ftds_include_dir ${GLOBAL_CACHE_DIR}/src/include/infras/ftds)
set(lib_dplog_include_dir ${GLOBAL_CACHE_DIR}/src/include/infras/infrastructure/log)
set(CMAKE_SKIP_RPATH true)
set(ceph_proxy_srcs CephProxyInterface.cc
                    CephProxy.cc
                    CephProxyOp.cc
                    RadosWorker.cc
                    RadosWrapper.cc
		    RadosMonitor.cc
		    ConfigRead.cc
		    CephProxyLog.h)

if (NOT RADOS_DIR)
	set(RADOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ceph-14.2.8)
endif()
message("RADOS_DIR=${RADOS_DIR}")
find_library(RADOS_LIB
    NAMES
    librados.so.2
    PATHS
    ${RADOS_DIR}/build/lib
    NO_DEFAULT_PATH
)

add_library(proxy SHARED ${ceph_proxy_srcs})
if (${CMAKE_BUILD_TYPE} STREQUAL "RELEASE")
    message(STATUS "Proxy cmake this is Release")
    target_compile_options(proxy PRIVATE -std=c++17 -g -DSYS_LOG
	    				-fstack-protector-all
					-fno-omit-frame-pointer
					-fPIC
					-D_FORTIFY_SOURCE=2 -O2)
    target_compile_options(proxy PRIVATE -DFTDS_ON)

    target_link_options(proxy PRIVATE -Wl,-z,relro,-z,now
	    			      -Wl,-z,noexecstack
				      -pie)
else()
    message(STATUS "Proxy cmake this is Debug")

    target_compile_options(proxy PRIVATE -std=c++17 -g -ftrapv -DSYS_LOG)
endif()

target_include_directories(proxy PRIVATE ${lib_rados_include_dir}
					 ${lib_dpax_include_dir}
					 ${lib_lvos_include_idr}
					 ${lib_ftds_include_dir}
					 ${lib_dplog_include_dir})

target_link_libraries(proxy PUBLIC ${RADOS_LIB} -lpthread)
