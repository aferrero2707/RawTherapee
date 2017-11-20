#! /bin/bash
(mkdir -p /work/w64-build && cd /work/w64-build) || exit 1
echo "Compiling RawTherapee"
ls /sources
(crossroad cmake -DCMAKE_BUILD_TYPE=Release -DPROC_TARGET_NUMBER=1 \
 -DCACHE_NAME_SUFFIX="'5-dev'" \
 -DCMAKE_C_FLAGS="'-mwin32 -m64 -mthreads -msse2'" \
 -DCMAKE_C_FLAGS_RELEASE="'-DNDEBUG -O2'" \
 -DCMAKE_CXX_FLAGS="'-mwin32 -m64 -mthreads -msse2'" \
 -DCMAKE_CXX_FLAGS_RELEASE="'-Wno-aggressive-loop-optimizations -DNDEBUG -O3'" \
 -DCMAKE_EXE_LINKER_FLAGS="'-m64 -mthreads -static-libgcc'" \
 -DCMAKE_EXE_LINKER_FLAGS_RELEASE="'-s -O3'" \
 /sources && make -j 2 && make install) || exit 1
