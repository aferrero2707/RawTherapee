/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2013 Klaus Post

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

#include "parsers/X3fParser.h"
#include "common/Common.h"              // for uint32, uchar8, make_unique
#include "decoders/RawDecoder.h"        // for RawDecoder
#include "decoders/X3fDecoder.h"        // for X3fDecoder
#include "io/Buffer.h"                  // for Buffer
#include "io/ByteStream.h"              // for ByteStream
#include "io/Endianness.h"              // for getHostEndianness, Endiann...
#include "io/IOException.h"             // for IOException
#include "parsers/X3fParserException.h" // for X3fParserException
#include <algorithm>                    // for move
#include <cassert>                      // for assert
#include <cstring>                      // for memset
#include <map>                          // for map, map<>::mapped_type
#include <string>                       // for string, basic_string, oper...
#include <vector>                       // for vector

using std::string;

namespace rawspeed {

X3fParser::X3fParser(const Buffer* file) : RawParser(file) {
  uint32 size = file->getSize();
  if (size < 104 + 128)
    ThrowXPE("X3F file too small");

  bytes = ByteStream(file, 0, size, Endianness::little);

  try {
    // Read signature
    if (bytes.getU32() != 0x62564f46)
      ThrowXPE("Not an X3f file (Signature)");

    uint32 version = bytes.getU32();
    if (version < 0x00020000)
      ThrowXPE("File version too old");

    // Skip identifier + mark bits
    bytes.skipBytes(16 + 4);

    bytes.setPosition(0);
  } catch (IOException& e) {
    ThrowXPE("IO Error while reading header: %s", e.what());
  }
}

static string getIdAsString(ByteStream *bytes) {
  uchar8 id[5];
  for (int i = 0; i < 4; i++)
    id[i] = bytes->getByte();
  id[4] = 0;
  return string(reinterpret_cast<const char*>(id));
}

void X3fParser::readDirectory(X3fDecoder* decoder) {
  bytes.setPosition(mInput->getSize() - 4);
  uint32 dir_off = bytes.getU32();
  bytes.setPosition(dir_off);

  // Check signature
  if ("SECd" != getIdAsString(&bytes))
    ThrowXPE("Unable to locate directory");

  uint32 version = bytes.getU32();
  if (version < 0x00020000)
    ThrowXPE("File version too old (directory)");

  uint32 n_entries = bytes.getU32();
  for (uint32 i = 0; i < n_entries; i++) {
    X3fDirectory dir(&bytes);
    decoder->mDirectory.push_back(dir);
    uint32 old_pos = bytes.getPosition();
    if ("IMA2" == dir.id || "IMAG" == dir.id) {
      decoder->mImages.emplace_back(&bytes, dir.offset, dir.length);
    }
    if ("PROP" == dir.id) {
      decoder->mProperties.addProperties(&bytes, dir.offset, dir.length);
    }
    bytes.setPosition(old_pos);
  }
}

std::unique_ptr<RawDecoder> X3fParser::getDecoder(const CameraMetaData* meta) {
  try {
    auto decoder = make_unique<X3fDecoder>(mInput);
    readDirectory(decoder.get());

    if (nullptr == decoder)
      ThrowXPE("No decoder found!");
    return std::move(decoder);
  } catch (IOException& e) {
    ThrowXPE("IO Error while reading header: %s", e.what());
  }
}

X3fDirectory::X3fDirectory( ByteStream *bytes )
{
    offset = bytes->getU32();
    length = bytes->getU32();
    id = getIdAsString(bytes);
    uint32 old_pos = bytes->getPosition();
    bytes->setPosition(offset);
    sectionID = getIdAsString(bytes);
    bytes->setPosition(old_pos);
}


X3fImage::X3fImage( ByteStream *bytes, uint32 offset, uint32 length )
{
  bytes->setPosition(offset);
  string id = getIdAsString(bytes);
  if (id != "SECi")
    ThrowXPE("Unknown Image signature");

  uint32 version = bytes->getU32();
  if (version < 0x00020000)
    ThrowXPE("File version too old (image)");

  type = bytes->getU32();
  format = bytes->getU32();
  width = bytes->getU32();
  height = bytes->getU32();
  pitchB = bytes->getU32();
  dataOffset = bytes->getPosition();
  dataSize = length - (dataOffset-offset);
  if (pitchB == dataSize)
    pitchB = 0;
}


/*
* ConvertUTF16toUTF8 function only Copyright:
*
* Copyright 2001-2004 Unicode, Inc.
*
* Disclaimer
*
* This source code is provided as is by Unicode, Inc. No claims are
* made as to fitness for any particular purpose. No warranties of any
* kind are expressed or implied. The recipient agrees to determine
* applicability of information provided. If this file has been
* purchased on magnetic or optical media from Unicode, Inc., the
* sole remedy for any claim will be exchange of defective media
* within 90 days of receipt.
*
* Limitations on Rights to Redistribute This Code
*
* Unicode, Inc. hereby grants the right to freely use the information
* supplied in this file in the creation of products supporting the
* Unicode Standard, and to make copies of this file in any form
* for internal or external distribution as long as this notice
* remains attached.
*/

using UTF32 = unsigned int;    /* at least 32 bits */
using UTF16 = unsigned short;  /* at least 16 bits */
using UTF8 = unsigned char;    /* typically 8 bits */
using Boolean = unsigned char; /* 0 or 1 */

/* Some fundamental constants */
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

#define UNI_MAX_UTF8_BYTES_PER_CODE_POINT 4

#define UNI_UTF16_BYTE_ORDER_MARK_NATIVE  0xFEFF
#define UNI_UTF16_BYTE_ORDER_MARK_SWAPPED 0xFFFE

#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF

static const int halfShift  = 10; /* used for shifting by 10 bits */
static const UTF32 halfBase = 0x0010000UL;
static const UTF8 firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

static bool ConvertUTF16toUTF8(const UTF16** sourceStart,
                               const UTF16* sourceEnd, UTF8** targetStart,
                               const UTF8* targetEnd) {
  bool success = true;
  const UTF16* source = *sourceStart;
  UTF8* target = *targetStart;
  while (source < sourceEnd) {
    UTF32 ch;
    unsigned short bytesToWrite = 0;
    const UTF32 byteMask = 0xBF;
    const UTF32 byteMark = 0x80;
    const UTF16* oldSource = source; /* In case we have to back up because of target overflow. */
    ch = *source;
    source++;
    /* If we have a surrogate pair, convert to UTF32 first. */
    if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) {
      /* If the 16 bits following the high surrogate are in the source buffer... */
      if (source < sourceEnd) {
        UTF32 ch2 = *source;
        /* If it's a low surrogate, convert to UTF32. */
        if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) {
          ch = ((ch - UNI_SUR_HIGH_START) << halfShift)
            + (ch2 - UNI_SUR_LOW_START) + halfBase;
          ++source;
#if 0
        } else if (flags == strictConversion) { /* it's an unpaired high surrogate */
          --source; /* return to the illegal value itself */
          success = false;
          break;
#endif
        }
      } else { /* We don't have the 16 bits following the high surrogate. */
        --source; /* return to the high surrogate */
        success = false;
        break;
      }
    }
    /* Figure out how many bytes the result will require */
    if (ch < static_cast<UTF32>(0x80)) {
      bytesToWrite = 1;
    } else if (ch < static_cast<UTF32>(0x800)) {
      bytesToWrite = 2;
    } else if (ch < static_cast<UTF32>(0x10000)) {
      bytesToWrite = 3;
    } else if (ch < static_cast<UTF32>(0x110000)) {
      bytesToWrite = 4;
    } else {                            bytesToWrite = 3;
    ch = UNI_REPLACEMENT_CHAR;
    }

    target += bytesToWrite;
    if (target > targetEnd) {
      source = oldSource; /* Back up source pointer! */
      target -= bytesToWrite;
      success = false;
      break;
    }
    assert(bytesToWrite > 0);
    for (int i = bytesToWrite; i > 1; i--) {
      target--;
      *target = static_cast<UTF8>((ch | byteMark) & byteMask);
      ch >>= 6;
    }
    target--;
    *target = static_cast<UTF8>(ch | firstByteMark[bytesToWrite]);
    target += bytesToWrite;
  }
  // Function modified to retain source + target positions
  //  *sourceStart = source;
  //  *targetStart = target;
  return success;
}

string X3fPropertyCollection::getString( ByteStream *bytes ) {
  uint32 max_len = bytes->getRemainSize() / 2;
  const auto* start =
      reinterpret_cast<const UTF16*>(bytes->getData(max_len * 2));
  const UTF16* src_end = start;
  uint32 i = 0;
  for (; i < max_len && start == src_end; i++) {
    if (start[i] == 0) {
      src_end = &start[i];
    }
  }
  if (start != src_end) {
    auto *dest = new UTF8[i * 4UL + 1];
    memset(dest, 0, i * 4UL + 1);
    if (ConvertUTF16toUTF8(&start, src_end, &dest, &dest[i * 4 - 1])) {
      string ret(reinterpret_cast<const char*>(dest));
      delete[] dest;
      return ret;
    }
    delete[] dest;
  }
  return "";
}

void X3fPropertyCollection::addProperties( ByteStream *bytes, uint32 offset, uint32 length )
{
  bytes->setPosition(offset);
  string id = getIdAsString(bytes);
  if (id != "SECp")
    ThrowXPE("Unknown Property signature");

  uint32 version = bytes->getU32();
  if (version < 0x00020000)
    ThrowXPE("File version too old (properties)");

  uint32 entries = bytes->getU32();
  if (!entries)
    return;

  if (0 != bytes->getU32())
    ThrowXPE("Unknown property character encoding");

  // Skip 4 reserved bytes
  bytes->skipBytes(4);

  // Skip size (not used ATM)
  bytes->skipBytes(4);

  if (entries > 1000)
    ThrowXPE("Unreasonable number of properties: %u", entries);

  uint32 data_start = bytes->getPosition() + entries*8;
  for (uint32 i = 0; i < entries; i++) {
    uint32 key_pos = bytes->getU32();
    uint32 value_pos = bytes->getU32();
    uint32 old_pos = bytes->getPosition();

    if (bytes->isValid(key_pos * 2 + data_start, 2) &&
        bytes->isValid(value_pos * 2 + data_start, 2)) {
      bytes->setPosition(key_pos * 2 + data_start);
      string key = getString(bytes);
      bytes->setPosition(value_pos * 2 + data_start);
      string val = getString(bytes);
      props[key] = val;
    }

    bytes->setPosition(old_pos);
  }
}

} // namespace rawspeed
