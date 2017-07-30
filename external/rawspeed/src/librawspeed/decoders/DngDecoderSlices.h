/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "rawspeedconfig.h"

#include "common/Common.h"    // for uint32
#include "common/RawImage.h"  // for RawImage
#include <memory>             // for unique_ptr
#include <queue>              // for queue
#include <vector>             // for vector

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

namespace rawspeed {

class Buffer;

class DngDecoderSlices;

class DngSliceElement
{
public:
  DngSliceElement(uint32 off, uint32 count, uint32 offsetX, uint32 offsetY,
                  uint32 w, uint32 h)
      : byteOffset(off), byteCount(count), offX(offsetX), offY(offsetY),
        width(w), height(h) {}
  const uint32 byteOffset;
  const uint32 byteCount;
  const uint32 offX;
  const uint32 offY;
  const uint32 width;
  const uint32 height;
};

class DngDecoderThread
{
public:
#ifdef HAVE_PTHREAD
  pthread_t threadid;
#endif
  explicit DngDecoderThread(DngDecoderSlices* parent_) : parent(parent_) {}
  std::queue<std::unique_ptr<DngSliceElement>> slices;
  DngDecoderSlices* parent;
};

void* DecodeThread(void* _this);

class DngDecoderSlices
{
public:
  DngDecoderSlices(const Buffer* file, const RawImage& img, int compression);
  void addSlice(std::unique_ptr<DngSliceElement>&& slice);
  void startDecoding();
  void decodeSlice(DngDecoderThread* t);
  int __attribute__((pure)) size();
  std::queue<std::unique_ptr<DngSliceElement>> slices;
  std::vector<std::unique_ptr<DngDecoderThread>> threads;
  const Buffer* mFile;
  RawImage mRaw;
  bool mFixLjpeg;
  uint32 mPredictor;
  uint32 mBps;
  uint32 nThreads;
  int compression;
};

} // namespace rawspeed
