#!/bin/bash
set +e
set -x

diff ../global_cache_sa/src/adaptor/sa/sa_export.h ./src/dependency/include/sa_export.h
diff ../global_cache_sa/src/adaptor/sa/sa_def.h ./src/dependency/include/sa_def.h

exit 0

