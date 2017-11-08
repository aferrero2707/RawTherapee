#! /bin/bash
(mkdir -p /work/w64-build && cd /work/w64-build) || exit 1
echo "Compiling RawTherapee"
(crossroad cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPROC_TARGET_NUMBER=1 /sources && make -j 2 && make install) || exit 1
