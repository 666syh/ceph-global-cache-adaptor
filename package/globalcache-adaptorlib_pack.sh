#!/bin/bash
set -e

curd=$(pwd)
globalcache_adapter_dir=$curd/../

tar_mode=$1

function initialize()
{
        if [[ -d "$curd/globalcache-adaptorlib" ]]; then
                rm -rf $curd/globalcache-adaptorlib
        fi

        mkdir -p $curd/globalcache-adaptorlib

        # lianjieku
        cp -rf $globalcache_adapter_dir/build/lib/* $curd/globalcache-adaptorlib
}

function pack_lib()
{
        if [[ $tar_mode == "DEBUG" ]]
        then
                tar -zcvf globalcache-adaptorlib-debug-oe1.tar.gz globalcache-adaptorlib
        else
                tar -zcvf globalcache-adaptorlib-release-oe1.tar.gz globalcache-adaptorlib
        fi
}

function main()
{
        initialize
        pack_lib
}

main $@
