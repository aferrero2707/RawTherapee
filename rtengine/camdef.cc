/*
 *  This file is part of RawTherapee.
 */
#include "camdef.h"
#include "settings.h"
#include "rt_math.h"
#include <cstdio>
#include <cstring>

// cJSON is a very minimal JSON parser lib in C, not for threaded stuff etc, so if we're going to use JSON more than just
// here we should probably replace cJSON with something beefier.
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

namespace rtengine
{


CameraDefinitions::CameraDefinitions(): meta(nullptr)
{
}


CameraDefinitions::~CameraDefinitions()
{
}

void CameraDefinitions::init(Glib::ustring baseDir)
{
  Glib::ustring camfile = Glib::build_filename(baseDir, "share/darktable/rawspeed/cameras.xml");
  printf("Loading RawSpeed camera defintions from %s", camfile.c_str());
  meta = new rawspeed::CameraMetaData(camfile.c_str());
  if( meta ) printf(" -> success\n");
  else printf(" -> FAILED\n");
}

CameraDefinitions *
CameraDefinitions::getInstance()
{
    static CameraDefinitions instance_;
    return &instance_;
}

} // namespace rtengine
