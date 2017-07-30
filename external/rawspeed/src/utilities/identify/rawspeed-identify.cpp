/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.
    copyright (c) 2016 Pedro Côrte-Real

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "RawSpeed-API.h" // for RawImage, RawImageData, iPoint2D, ImageMet...

#include <cstddef>    // for size_t
#include <cstdint>    // for uint16_t
#include <cstdio>     // for fprintf, stdout, stderr, printf
#include <memory>     // for unique_ptr
#include <string>     // for string, operator+
#include <sys/stat.h> // for stat

#ifdef _OPENMP
#include <omp.h>
#endif

// define this function, it is only declared in rawspeed:
#ifdef _OPENMP
extern "C" int rawspeed_get_number_of_processor_cores() {
  return omp_get_num_procs();
}
#else
extern "C" int __attribute__((const)) rawspeed_get_number_of_processor_cores() {
  return 1;
}
#endif

namespace rawspeed {

namespace identify {

std::string find_cameras_xml(const char* argv0);

std::string find_cameras_xml(const char *argv0) {
  struct stat statbuf;

#ifdef RS_CAMERAS_XML_PATH
  static const char set_camfile[] = RS_CAMERAS_XML_PATH;
  if (stat(set_camfile, &statbuf)) {
    fprintf(stderr, "WARNING: Couldn't find cameras.xml in '%s'\n",
            set_camfile);
  } else {
    return set_camfile;
  }
#endif

  const std::string self(argv0);

  // If we haven't been provided with a valid cameras.xml path on compile try
  // relative to argv[0]
  const std::size_t lastslash = self.find_last_of(R"(/\)");
  const std::string bindir(self.substr(0, lastslash));

  std::string found_camfile(bindir +
                            "/../share/darktable/rawspeed/cameras.xml");

  if (stat(found_camfile.c_str(), &statbuf)) {
#ifndef __APPLE__
    fprintf(stderr, "WARNING: Couldn't find cameras.xml in '%s'\n",
            found_camfile.c_str());
#else
    fprintf(stderr, "WARNING: Couldn't find cameras.xml in '%s'\n",
            found_camfile.c_str());
    found_camfile =
        bindir + "/../Resources/share/darktable/rawspeed/cameras.xml";
    if (stat(found_camfile.c_str(), &statbuf)) {
      fprintf(stderr, "WARNING: Couldn't find cameras.xml in '%s'\n",
              found_camfile.c_str());
    }
#endif
  }

  // running from build dir?
  found_camfile = std::string(CMAKE_SOURCE_DIR "/data/cameras.xml");

  if (stat(found_camfile.c_str(), &statbuf)) {
#ifndef __APPLE__
    fprintf(stderr, "ERROR: Couldn't find cameras.xml in '%s'\n",
            found_camfile.c_str());
    return nullptr;
#else
    fprintf(stderr, "WARNING: Couldn't find cameras.xml in '%s'\n",
            found_camfile.c_str());
    found_camfile =
        bindir + "/../Resources/share/darktable/rawspeed/cameras.xml";
    if (stat(found_camfile.c_str(), &statbuf)) {
      fprintf(stderr, "ERROR: Couldn't find cameras.xml in '%s'\n",
              found_camfile.c_str());
      return nullptr;
    }
#endif
  }

  return found_camfile;
}

} // namespace identify

} // namespace rawspeed

using rawspeed::CameraMetaData;
using rawspeed::FileReader;
using rawspeed::RawParser;
using rawspeed::RawImage;
using rawspeed::uchar8;
using rawspeed::uint32;
using rawspeed::iPoint2D;
using rawspeed::TYPE_USHORT16;
using rawspeed::TYPE_FLOAT32;
using rawspeed::RawspeedException;
using rawspeed::identify::find_cameras_xml;

int main(int argc, char *argv[]) {

  if (argc != 2) {
    fprintf(stderr, "Usage: darktable-rs-identify <file>\n");
    return 0;
  }

  const std::string camfile = find_cameras_xml(argv[0]);
  if (camfile.empty()) {
    // fprintf(stderr, "ERROR: Couldn't find cameras.xml\n");
    return 2;
  }
  // fprintf(stderr, "Using cameras.xml from '%s'\n", camfile);

  try {
    std::unique_ptr<const CameraMetaData> meta;

#ifdef HAVE_PUGIXML
    meta = rawspeed::make_unique<CameraMetaData>(camfile.c_str());
#else
    meta = rawspeed::make_unique<CameraMetaData>();
#endif

    if (!meta.get()) {
      fprintf(stderr, "ERROR: Couldn't get a CameraMetaData instance\n");
      return 2;
    }

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    fprintf(stderr, "Loading file: \"%s\"\n", argv[1]);

    FileReader f(argv[1]);

    auto m(f.readFile());

    RawParser t(m.get());

    auto d(t.getDecoder(meta.get()));

    if (!d.get()) {
      fprintf(stderr, "ERROR: Couldn't get a RawDecoder instance\n");
      return 2;
    }

    d->applyCrop = false;
    d->failOnUnknown = true;
    RawImage r = d->mRaw;

    d->decodeMetaData(meta.get());

    fprintf(stdout, "make: %s\n", r->metadata.make.c_str());
    fprintf(stdout, "model: %s\n", r->metadata.model.c_str());

    fprintf(stdout, "canonical_make: %s\n", r->metadata.canonical_make.c_str());
    fprintf(stdout, "canonical_model: %s\n",
            r->metadata.canonical_model.c_str());
    fprintf(stdout, "canonical_alias: %s\n",
            r->metadata.canonical_alias.c_str());

    d->checkSupport(meta.get());
    d->decodeRaw();
    d->decodeMetaData(meta.get());
    r = d->mRaw;

    const auto errors = r->getErrors();
    for (auto& error : errors)
      fprintf(stderr, "WARNING: [rawspeed] %s\n", error.c_str());

    fprintf(stdout, "blackLevel: %d\n", r->blackLevel);
    fprintf(stdout, "whitePoint: %d\n", r->whitePoint);

    fprintf(stdout, "blackLevelSeparate: %d %d %d %d\n",
            r->blackLevelSeparate[0], r->blackLevelSeparate[1],
            r->blackLevelSeparate[2], r->blackLevelSeparate[3]);

    fprintf(stdout, "wbCoeffs: %f %f %f %f\n", r->metadata.wbCoeffs[0],
            r->metadata.wbCoeffs[1], r->metadata.wbCoeffs[2],
            r->metadata.wbCoeffs[3]);

    fprintf(stdout, "isCFA: %d\n", r->isCFA);
    uint32 filters = r->cfa.getDcrawFilter();
    fprintf(stdout, "filters: %d (0x%x)\n", filters, filters);
    const uint32 bpp = r->getBpp();
    fprintf(stdout, "bpp: %d\n", bpp);
    uint32 cpp = r->getCpp();
    fprintf(stdout, "cpp: %d\n", cpp);
    fprintf(stdout, "dataType: %d\n", r->getDataType());

    // dimensions of uncropped image
    const iPoint2D dimUncropped = r->getUncroppedDim();
    fprintf(stdout, "dimUncropped: %dx%d\n", dimUncropped.x, dimUncropped.y);

    // dimensions of cropped image
    iPoint2D dimCropped = r->dim;
    fprintf(stdout, "dimCropped: %dx%d\n", dimCropped.x, dimCropped.y);

    // crop - Top,Left corner
    iPoint2D cropTL = r->getCropOffset();
    fprintf(stdout, "cropOffset: %dx%d\n", cropTL.x, cropTL.y);

    fprintf(stdout, "fuji_rotation_pos: %d\n", r->metadata.fujiRotationPos);
    fprintf(stdout, "pixel_aspect_ratio: %f\n", r->metadata.pixelAspectRatio);

    double sum = 0.0F;
    {
      uchar8 *const data = r->getDataUncropped(0, 0);

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static) reduction(+ : sum)
#endif
      for (size_t k = 0;
           k < (static_cast<size_t>(dimUncropped.y) * dimUncropped.x * bpp);
           k++) {
        sum += static_cast<double>(data[k]);
      }
    }
    fprintf(stdout, "Image byte sum: %lf\n", sum);
    fprintf(stdout, "Image byte avg: %lf\n",
            sum / static_cast<double>(dimUncropped.y * dimUncropped.x * bpp));

    if (r->getDataType() == TYPE_FLOAT32) {
      sum = 0.0F;
      auto* const data = reinterpret_cast<float*>(r->getDataUncropped(0, 0));

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static) reduction(+ : sum)
#endif
      for (size_t k = 0;
           k < (static_cast<size_t>(dimUncropped.y) * dimUncropped.x); k++) {
        sum += static_cast<double>(data[k]);
      }

      fprintf(stdout, "Image float sum: %lf\n", sum);
      fprintf(stdout, "Image float avg: %lf\n",
              sum / static_cast<double>(dimUncropped.y * dimUncropped.x));
    } else if (r->getDataType() == TYPE_USHORT16) {
      sum = 0.0F;
      auto* const data = reinterpret_cast<uint16_t*>(r->getDataUncropped(0, 0));

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static) reduction(+ : sum)
#endif
      for (size_t k = 0;
           k < (static_cast<size_t>(dimUncropped.y) * dimUncropped.x); k++) {
        sum += static_cast<double>(data[k]);
      }

      fprintf(stdout, "Image uint16_t sum: %lf\n", sum);
      fprintf(stdout, "Image uint16_t avg: %lf\n",
              sum / static_cast<double>(dimUncropped.y * dimUncropped.x));
    }
  } catch (RawspeedException& e) {
    fprintf(stderr, "ERROR: [rawspeed] %s\n", e.what());

    /* if an exception is raised lets not retry or handle the
     specific ones, consider the file as corrupted */
    return 0;
  }

  return 0;
}

// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: indent-mode cstyle; replace-tabs on; tab-indents: off;
// kate: remove-trailing-space on;
