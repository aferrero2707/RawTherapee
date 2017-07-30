/*
 *  This file is part of RawTherapee.
 */
#ifndef __CAMDEF__
#define __CAMDEF__

#include <glibmm.h>
#include <map>

#include "RawSpeed-API.h"


namespace rtengine
{

class CameraDefinitions
{
private:
  rawspeed::CameraMetaData *meta;
  CameraDefinitions();

public:
    ~CameraDefinitions();
    void init(Glib::ustring baseDir);
    static CameraDefinitions *getInstance(void);
    rawspeed::CameraMetaData* getCameraMetaData() { return meta; }
};

}

#endif
