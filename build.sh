#!/bin/bash
# Copyright c Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
# Description: The script of building global_cache_adaptor
set -ex
set -o pipefail
debug=$1
FILE_PATH="$(readlink -f $(dirname $0))"
BUILD_DIR="${FILE_PATH}/build"
ROOT_DIR=${FILE_PATH}
CMAKE_ROOT_DIR={ROOT_DIR}
TEST_BIN_DIR="${FILE_PATH}/test/bin"

main()
{
        rm -rf ${BUILD_DIR}
        mkdir -p ${BUILD_DIR}
        rm -rf ${TEST_BIN_DIR}

        cd ${BUILD_DIR}
        if type cmake3 > /dev/null 2>&1 ; then
                CMAKE-cmake3
                echo "Using cmake3."
        else
                CMAKE-cmake
                echo "Using cmake."
        fi
        if [ "$debug" = "DEBUG" ] ; then
                echo "Build global_cache_adaptor DEBUG."
                ${CMAKE} ${CMAKE_ROOT_DIR} -DCMAKE_SKIP_RPATH=true -DDEPENDENCY_GC=true
        else
                echo "Build global_cache_adaptor RELEASE."
                ${CMAKE} ${CMAKE_ROOT_DIR} -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_SKIP_RPATH=true -DDEPENDENCY_GC=true
        fi
        make -j16
}

main $*
exit 0



