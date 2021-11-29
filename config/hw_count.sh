#!/bin/bash

which papi_avail >& /dev/null
if [ $? -eq 0 ]; then
  HWCOUNT=`papi_avail | grep "Number Hardware Counters" | cut -f2 -d:`
else
  HWCOUNT=0
fi
echo "#define KAAPI_MAX_HWCOUNTERS  $HWCOUNT" > $1
