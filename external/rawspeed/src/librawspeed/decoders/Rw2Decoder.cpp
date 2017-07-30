/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post
    Copyright (C) 2014 Pedro Côrte-Real

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

#include "decoders/Rw2Decoder.h"
#include "common/Common.h"                          // for uint32, uchar8
#include "common/Mutex.h"                           // for MutexLocker
#include "common/Point.h"                           // for iPoint2D
#include "decoders/RawDecoder.h"                    // for RawDecoderThread
#include "decoders/RawDecoderException.h"           // for RawDecoderExcept...
#include "decompressors/UncompressedDecompressor.h" // for UncompressedDeco...
#include "io/Buffer.h"                              // for Buffer
#include "io/ByteStream.h"                          // for ByteStream
#include "io/Endianness.h"                          // for Endianness
#include "metadata/Camera.h"                        // for Hints
#include "metadata/ColorFilterArray.h"              // for CFAColor::CFA_GREEN
#include "tiff/TiffEntry.h"                         // for TiffEntry
#include "tiff/TiffIFD.h"                           // for TiffIFD, TiffRoo...
#include "tiff/TiffTag.h"                           // for TiffTag, TiffTag...
#include <algorithm>                                // for min, move
#include <cmath>                                    // for fabs
#include <cstring>                                  // for memcpy
#include <memory>                                   // for unique_ptr
#include <string>                                   // for string, allocator
#include <vector>                                   // for vector

using std::vector;
using std::move;
using std::min;
using std::string;
using std::fabs;

namespace rawspeed {

class CameraMetaData;

bool Rw2Decoder::isAppropriateDecoder(const TiffRootIFD* rootIFD,
                                      const Buffer* file) {
  const auto id = rootIFD->getID();
  const std::string& make = id.make;

  // FIXME: magic

  return make == "Panasonic" || make == "LEICA";
}

struct Rw2Decoder::PanaBitpump {
  static constexpr uint32 BufSize = 0x4000;
  ByteStream input;
  vector<uchar8> buf;
  int vbits = 0;
  uint32 load_flags;

  PanaBitpump(ByteStream&& input_, int load_flags_)
    : input(move(input_)), load_flags(load_flags_)
  {
    // get one more byte, so the return statement of getBits does not have
    // to special case for accessing the last byte
    buf.resize(BufSize + 1UL);
  }

  void skipBytes(int bytes)
  {
    int blocks = (bytes / BufSize) * BufSize;
    input.skipBytes(blocks);
    for (int i = blocks; i < bytes; i++)
      (void)getBits(8);
  }

  uint32 getBits(int nbits)
  {
    if (!vbits) {
      /* On truncated files this routine will just return just for the truncated
      * part of the file. Since there is no chance of affecting output buffer
      * size we allow the decoder to decode this
      */
      auto size = min(input.getRemainSize(), BufSize - load_flags);
      memcpy(buf.data() + load_flags, input.getData(size), size);

      size = min(input.getRemainSize(), load_flags);
      if (size != 0)
        memcpy(buf.data(), input.getData(size), size);
    }
    vbits = (vbits - nbits) & 0x1ffff;
    int byte = vbits >> 3 ^ 0x3ff0;
    return (buf[byte] | buf[byte + 1UL] << 8) >> (vbits & 7) & ~(-(1 << nbits));
  }
};

RawImage Rw2Decoder::decodeRawInternal() {

  const TiffIFD* raw = nullptr;
  bool isOldPanasonic = ! mRootIFD->hasEntryRecursive(PANASONIC_STRIPOFFSET);

  if (! isOldPanasonic)
    raw = mRootIFD->getIFDWithTag(PANASONIC_STRIPOFFSET);
  else
    raw = mRootIFD->getIFDWithTag(STRIPOFFSETS);

  uint32 height = raw->getEntry(static_cast<TiffTag>(3))->getU16();
  uint32 width = raw->getEntry(static_cast<TiffTag>(2))->getU16();

  if (isOldPanasonic) {
    TiffEntry *offsets = raw->getEntry(STRIPOFFSETS);

    if (offsets->count != 1) {
      ThrowRDE("Multiple Strips found: %u", offsets->count);
    }
    offset = offsets->getU32();
    if (!mFile->isValid(offset))
      ThrowRDE("Invalid image data offset, cannot decode.");

    mRaw->dim = iPoint2D(width, height);
    mRaw->createData();

    uint32 size = mFile->getSize() - offset;

    UncompressedDecompressor u(ByteStream(mFile, offset), mRaw);

    if (size >= width*height*2) {
      // It's completely unpacked little-endian
      u.decodeRawUnpacked<12, Endianness::little>(width, height);
    } else if (size >= width*height*3/2) {
      // It's a packed format
      u.decode12BitRaw<Endianness::little, false, true>(width, height);
    } else {
      // It's using the new .RW2 decoding method
      load_flags = 0;
      DecodeRw2();
    }
  } else {

    mRaw->dim = iPoint2D(width, height);
    mRaw->createData();
    TiffEntry *offsets = raw->getEntry(PANASONIC_STRIPOFFSET);

    if (offsets->count != 1) {
      ThrowRDE("Multiple Strips found: %u", offsets->count);
    }

    offset = offsets->getU32();

    if (!mFile->isValid(offset))
      ThrowRDE("Invalid image data offset, cannot decode.");

    load_flags = 0x2008;
    DecodeRw2();
  }

  return mRaw;
}

void Rw2Decoder::DecodeRw2() {
  startThreads();
}

void Rw2Decoder::decodeThreaded(RawDecoderThread * t) {
  int x;
  int i;
  int j;
  int sh = 0;
  int pred[2];
  int nonz[2];
  int w = mRaw->dim.x / 14;
  uint32 y;

  bool zero_is_bad = ! hints.has("zero_is_not_bad");

  /* 9 + 1/7 bits per pixel */
  int skip = w * 14 * t->start_y * 9;
  skip += w * 2 * t->start_y;
  skip /= 8;

  PanaBitpump bits(ByteStream(mFile, offset), load_flags);
  bits.skipBytes(skip);

  vector<uint32> zero_pos;
  for (y = t->start_y; y < t->end_y; y++) {
    auto* dest = reinterpret_cast<ushort16*>(mRaw->getData(0, y));
    for (x = 0; x < w; x++) {
      pred[0] = pred[1] = nonz[0] = nonz[1] = 0;
      int u = 0;
      for (i = 0; i < 14;) {
        for (int c = 0; c < 2; c++) {
          if (u == 2) {
            sh = 4 >> (3 - bits.getBits(2));
            u = -1;
          }

          if (nonz[c]) {
            if ((j = bits.getBits(8))) {
              if ((pred[c] -= 0x80 << sh) < 0 || sh == 4)
                pred[c] &= ~(-(1 << sh));
              pred[c] += j << sh;
            }
          } else if ((nonz[c] = bits.getBits(8)) || i > 11)
            pred[c] = nonz[c] << 4 | bits.getBits(4);

          *dest = pred[c];

          if (zero_is_bad && 0 == pred[c])
            zero_pos.push_back((y << 16) | (x * 14 + i));

          i++;
          u++;
          dest++;
        }
      }
    }
  }
  if (zero_is_bad && !zero_pos.empty()) {
    MutexLocker guard(&mRaw->mBadPixelMutex);
    mRaw->mBadPixelPositions.insert(mRaw->mBadPixelPositions.end(), zero_pos.begin(), zero_pos.end());
  }
}

void Rw2Decoder::checkSupportInternal(const CameraMetaData* meta) {
  auto id = mRootIFD->getID();
  if (!checkCameraSupported(meta, id, guessMode()))
    checkCameraSupported(meta, id, "");
}

void Rw2Decoder::decodeMetaDataInternal(const CameraMetaData* meta) {
  mRaw->cfa.setCFA(iPoint2D(2,2), CFA_BLUE, CFA_GREEN, CFA_GREEN, CFA_RED);

  auto id = mRootIFD->getID();
  string mode = guessMode();
  int iso = 0;
  if (mRootIFD->hasEntryRecursive(PANASONIC_ISO_SPEED))
    iso = mRootIFD->getEntryRecursive(PANASONIC_ISO_SPEED)->getU32();

  if (this->checkCameraSupported(meta, id, mode)) {
    setMetaData(meta, id, mode, iso);
  } else {
    mRaw->metadata.mode = mode;
    writeLog(DEBUG_PRIO_EXTRA, "Mode not found in DB: %s", mode.c_str());
    setMetaData(meta, id, "", iso);
  }

  const TiffIFD* raw = mRootIFD->hasEntryRecursive(PANASONIC_STRIPOFFSET)
                           ? mRootIFD->getIFDWithTag(PANASONIC_STRIPOFFSET)
                           : mRootIFD->getIFDWithTag(STRIPOFFSETS);

  // Read blacklevels
  if (raw->hasEntry(static_cast<TiffTag>(0x1c)) &&
      raw->hasEntry(static_cast<TiffTag>(0x1d)) &&
      raw->hasEntry(static_cast<TiffTag>(0x1e))) {
    const int blackRed =
        raw->getEntry(static_cast<TiffTag>(0x1c))->getU32() + 15;
    const int blackGreen =
        raw->getEntry(static_cast<TiffTag>(0x1d))->getU32() + 15;
    const int blackBlue =
        raw->getEntry(static_cast<TiffTag>(0x1e))->getU32() + 15;

    for(int i = 0; i < 2; i++) {
      for(int j = 0; j < 2; j++) {
        const int k = i + 2 * j;
        const CFAColor c = mRaw->cfa.getColorAt(i, j);
        switch (c) {
          case CFA_RED:
            mRaw->blackLevelSeparate[k] = blackRed;
            break;
          case CFA_GREEN:
            mRaw->blackLevelSeparate[k] = blackGreen;
            break;
          case CFA_BLUE:
            mRaw->blackLevelSeparate[k] = blackBlue;
            break;
          default:
            ThrowRDE("Unexpected CFA color %s.",
                     ColorFilterArray::colorToString(c).c_str());
            break;
        }
      }
    }
  }

  // Read WB levels
  if (raw->hasEntry(static_cast<TiffTag>(0x0024)) &&
      raw->hasEntry(static_cast<TiffTag>(0x0025)) &&
      raw->hasEntry(static_cast<TiffTag>(0x0026))) {
    mRaw->metadata.wbCoeffs[0] = static_cast<float>(
        raw->getEntry(static_cast<TiffTag>(0x0024))->getU16());
    mRaw->metadata.wbCoeffs[1] = static_cast<float>(
        raw->getEntry(static_cast<TiffTag>(0x0025))->getU16());
    mRaw->metadata.wbCoeffs[2] = static_cast<float>(
        raw->getEntry(static_cast<TiffTag>(0x0026))->getU16());
  } else if (raw->hasEntry(static_cast<TiffTag>(0x0011)) &&
             raw->hasEntry(static_cast<TiffTag>(0x0012))) {
    mRaw->metadata.wbCoeffs[0] = static_cast<float>(
        raw->getEntry(static_cast<TiffTag>(0x0011))->getU16());
    mRaw->metadata.wbCoeffs[1] = 256.0F;
    mRaw->metadata.wbCoeffs[2] = static_cast<float>(
        raw->getEntry(static_cast<TiffTag>(0x0012))->getU16());
  }
}

std::string Rw2Decoder::guessMode() {
  float ratio = 3.0F / 2.0F; // Default

  if (!mRaw->isAllocated())
    return "";

  ratio = static_cast<float>(mRaw->dim.x) / static_cast<float>(mRaw->dim.y);

  float min_diff = fabs(ratio - 16.0F / 9.0F);
  std::string closest_match = "16:9";

  float t = fabs(ratio - 3.0F / 2.0F);
  if (t < min_diff) {
    closest_match = "3:2";
    min_diff  = t;
  }

  t = fabs(ratio - 4.0F / 3.0F);
  if (t < min_diff) {
    closest_match =  "4:3";
    min_diff  = t;
  }

  t = fabs(ratio - 1.0F);
  if (t < min_diff) {
    closest_match = "1:1";
    min_diff  = t;
  }
  writeLog(DEBUG_PRIO_EXTRA, "Mode guess: '%s'", closest_match.c_str());
  return closest_match;
}

} // namespace rawspeed
