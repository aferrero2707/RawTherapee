/*
 *  This file is part of RawTherapee.
 *
 *  Created on: 20/nov/2010
 */

#include <strings.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "rawimage_rawspeed.h"
#include "settings.h"
#include "camdef.h"
#include "camconst.h"
#include "utils.h"


// define this function, it is only declared in rawspeed:
int rawspeed_get_number_of_processor_cores()
{
#ifdef _OPENMP_
  return omp_get_num_procs();
#else
  return 1;
#endif
}

using namespace rawspeed;


typedef struct dt_image_t
{
  // minimal exif data here (all in multiples of 4-byte to interface nicely with c++):
  int32_t exif_inited;
  //dt_image_orientation_t orientation;
  float exif_exposure;
  float exif_aperture;
  float exif_iso;
  float exif_focal_length;
  float exif_focus_distance;
  float exif_crop;
  char exif_maker[64];
  char exif_model[64];
  char exif_lens[128];
  char exif_datetime_taken[20];

  char camera_maker[64];
  char camera_model[64];
  char camera_alias[64];
  char camera_makermodel[128];
  char camera_legacy_makermodel[128];

  char filename[1000];

  // common stuff

  // to understand this, look at comment for dt_histogram_roi_t
  int32_t width, height;
  int32_t crop_x, crop_y, crop_width, crop_height;

  // used by library
  int32_t num, flags, film_id, id, group_id, version;
  //dt_image_loader_t loader;

  //dt_iop_buffer_dsc_t buf_dsc;

  float d65_color_matrix[9]; // the 3x3 matrix embedded in some DNGs
  uint8_t *profile;          // embedded profile, for example from JPEGs
  uint32_t profile_size;
  //dt_image_colorspace_t colorspace; // the colorspace that is specified in exif. mostly used for jpeg files

  //dt_image_raw_parameters_t legacy_flip; // unfortunately needed to convert old bits to new flip module.

  /* gps coords */
  double longitude;
  double latitude;
  double elevation;

  /* needed in exposure iop for Deflicker */
  uint16_t raw_black_level;
  uint16_t raw_black_level_separate[4];
  uint32_t raw_white_point;

  /* needed to fix some manufacturers madness */
  uint32_t fuji_rotation_pos;
  float pixel_aspect_ratio;

  /* White balance coeffs from the raw */
  float wb_coeffs[4];
} dt_image_t;




namespace rtengine
{

extern const Settings* settings;

RawImage::RawImage(  const Glib::ustring &name )
: data(nullptr)
, prefilters(0)
, filename(name)
, rotate_deg(0)
, profile_data(nullptr)
, allocation(nullptr)
, ifp(nullptr)
{
  memset(maximum_c4, 0, sizeof(maximum_c4));
  RT_matrix_from_constant = 0;
  RT_blacklevel_from_constant = 0;
  RT_whitelevel_from_constant = 0;
}

RawImage::~RawImage()
{
  if(ifp) {
    fclose(ifp);
    ifp = nullptr;
  }

  if( image ) {
    free(image);
  }

  if(allocation) {
    delete [] allocation;
    allocation = nullptr;
  }

  if(float_raw_image) {
    delete [] float_raw_image;
    float_raw_image = nullptr;
  }

  if(data) {
    delete [] data;
    data = nullptr;
  }

  if(profile_data) {
    delete [] profile_data;
    profile_data = nullptr;
  }
}

eSensorType RawImage::getSensorType()
{
  if (isBayer()) {
    return ST_BAYER;
  } else if (isXtrans()) {
    return ST_FUJI_XTRANS;
  } else if (isFoveon()) {
    return ST_FOVEON;
  }

  return ST_NONE;
}

/* Similar to dcraw scale_colors for coeff. calculation, but without actual pixels scaling.
 * need pixels in data[][] available
 */
void RawImage::get_colorsCoeff( float *pre_mul_, float *scale_mul_, float *cblack_, bool forceAutoWB)
{
  unsigned sum[8], c;
  unsigned W = this->get_width();
  unsigned H = this->get_height();
  float val;
  double dsum[8], dmin, dmax;

  if(isXtrans()) {
    // for xtrans files dcraw stores black levels in cblack[6] .. cblack[41], but all are equal, so we just use cblack[6]
    for (int c = 0; c < 4; c++) {
      cblack_[c] = (float) this->get_cblack(6);
      pre_mul_[c] = this->get_pre_mul(c);
    }
  } else if ((this->get_cblack(4) + 1) / 2 == 1 && (this->get_cblack(5) + 1) / 2 == 1) {
    for (int c = 0; c < 4; c++) {
      cblack_[c] = this->get_cblack(c);
    }
    for (int c = 0; c < 4; c++) {
      cblack_[FC(c / 2, c % 2)] = this->get_cblack(6 + c / 2 % this->get_cblack(4) * this->get_cblack(5) + c % 2 % this->get_cblack(5));
      pre_mul_[c] = this->get_pre_mul(c);
    }
  } else {
    for (int c = 0; c < 4; c++) {
      cblack_[c] = (float) this->get_cblack(c);
      pre_mul_[c] = this->get_pre_mul(c);
    }
  }

  if ( this->get_cam_mul(0) == -1 || forceAutoWB) {
    memset(dsum, 0, sizeof dsum);

    if (this->isBayer()) {
      // calculate number of pixels per color
      dsum[FC(0, 0) + 4] += (int)(((W + 1) / 2) * ((H + 1) / 2));
      dsum[FC(0, 1) + 4] += (int)(((W / 2) * ((H + 1) / 2)));
      dsum[FC(1, 0) + 4] += (int)(((W + 1) / 2) * (H / 2));
      dsum[FC(1, 1) + 4] += (int)((W / 2) * (H / 2));

#pragma omp parallel private(val)
      {
        double dsumthr[8];
        memset(dsumthr, 0, sizeof dsumthr);
        float sum[4];
        // make local copies of the black and white values to avoid calculations and conversions
        float cblackfloat[4];
        float whitefloat[4];

        for (int c = 0; c < 4; c++) {
          cblackfloat[c] = cblack_[c];
          whitefloat[c] = this->get_white(c) - 25;
        }

        float *tempdata = data[0];
#pragma omp for nowait

        for (size_t row = 0; row < H; row += 8) {
          size_t ymax = row + 8 < H ? row + 8 : H;

          for (size_t col = 0; col < W ; col += 8) {
            size_t xmax = col + 8 < W ? col + 8 : W;
            memset(sum, 0, sizeof sum);

            for (size_t y = row; y < ymax; y++)
              for (size_t x = col; x < xmax; x++) {
                int c = FC(y, x);
                val = tempdata[y * W + x];

                if (val > whitefloat[c]) { // calculate number of pixels to be substracted from sum and skip the block
                  dsumthr[FC(row, col) + 4]      += (int)(((xmax - col + 1) / 2) * ((ymax - row + 1) / 2));
                  dsumthr[FC(row, col + 1) + 4]    += (int)(((xmax - col) / 2) * ((ymax - row + 1) / 2));
                  dsumthr[FC(row + 1, col) + 4]    += (int)(((xmax - col + 1) / 2) * ((ymax - row) / 2));
                  dsumthr[FC(row + 1, col + 1) + 4]  += (int)(((xmax - col) / 2) * ((ymax - row) / 2));
                  goto skip_block2;
                }

                if (val < cblackfloat[c]) {
                  val = cblackfloat[c];
                }

                sum[c] += val;
              }

            for (int c = 0; c < 4; c++) {
              dsumthr[c] += sum[c];
            }

            skip_block2:
            ;
          }
        }

#pragma omp critical
        {
          for (int c = 0; c < 4; c++) {
            dsum[c] += dsumthr[c];
          }

          for (int c = 4; c < 8; c++) {
            dsum[c] -= dsumthr[c];
          }

        }
      }

      for(int c = 0; c < 4; c++) {
        dsum[c] -= cblack_[c] * dsum[c + 4];
      }

    } else if(isXtrans()) {
#pragma omp parallel
      {
        double dsumthr[8];
        memset(dsumthr, 0, sizeof dsumthr);
        float sum[8];
        // make local copies of the black and white values to avoid calculations and conversions
        float whitefloat[4];

        for (int c = 0; c < 4; c++)
        {
          whitefloat[c] = this->get_white(c) - 25;
        }

#pragma omp for nowait

        for (size_t row = 0; row < H; row += 8)
          for (size_t col = 0; col < W ; col += 8)
          {
            memset(sum, 0, sizeof sum);

            for (size_t y = row; y < row + 8 && y < H; y++)
              for (size_t x = col; x < col + 8 && x < W; x++) {
                int c = XTRANSFC(y, x);
                float val = data[y][x];

                if (val > whitefloat[c]) {
                  goto skip_block3;
                }

                if ((val -= cblack_[c]) < 0) {
                  val = 0;
                }

                sum[c] += val;
                sum[c + 4]++;
              }

            for (int c = 0; c < 8; c++) {
              dsumthr[c] += sum[c];
            }

            skip_block3:
            ;
          }

#pragma omp critical
        {
          for (int c = 0; c < 8; c++)
          {
            dsum[c] += dsumthr[c];
          }

        }
      }
    } else if (colors == 1) {
      for (int c = 0; c < 4; c++) {
        pre_mul_[c] = 1;
      }
    } else {
      for (size_t row = 0; row < H; row += 8)
        for (size_t col = 0; col < W ; col += 8) {
          memset(sum, 0, sizeof sum);

          for (size_t y = row; y < row + 8 && y < H; y++)
            for (size_t x = col; x < col + 8 && x < W; x++)
              for (int c = 0; c < 3; c++) {
                if (this->isBayer()) {
                  c = FC(y, x);
                  val = data[y][x];
                } else {
                  val = data[y][3 * x + c];
                }

                if (val > this->get_white(c) - 25) {
                  goto skip_block;
                }

                if ((val -= cblack_[c]) < 0) {
                  val = 0;
                }

                sum[c] += val;
                sum[c + 4]++;

                if ( this->isBayer()) {
                  break;
                }
              }

          for (c = 0; c < 8; c++) {
            dsum[c] += sum[c];
          }

          skip_block:
          ;
        }
    }

    for (int c = 0; c < 4; c++)
      if (dsum[c]) {
        pre_mul_[c] = dsum[c + 4] / dsum[c];
      }
  } else {
    memset(sum, 0, sizeof sum);

    for (size_t row = 0; row < 8; row++)
      for (size_t col = 0; col < 8; col++) {
        int c = FC(row, col);

        if ((val = white[row][col] - cblack_[c]) > 0) {
          sum[c] += val;
        }

        sum[c + 4]++;
      }

    if (sum[0] && sum[1] && sum[2] && sum[3])
      for (int c = 0; c < 4; c++) {
        pre_mul_[c] = (float) sum[c + 4] / sum[c];
      }
    else if (this->get_cam_mul(0) && this->get_cam_mul(2)) {
      pre_mul_[0] = this->get_cam_mul(0);
      pre_mul_[1] = this->get_cam_mul(1);
      pre_mul_[2] = this->get_cam_mul(2);
      pre_mul_[3] = this->get_cam_mul(3);
    } else {
      fprintf(stderr, "Cannot use camera white balance.\n");
    }
  }

  if (pre_mul_[3] == 0) {
    pre_mul_[3] = this->get_colors() < 4 ? pre_mul_[1] : 1;
  } else if (this->get_colors() < 4) {
    pre_mul_[3] = pre_mul_[1] = (pre_mul_[3] + pre_mul_[1]) / 2;
  }

  if (colors == 1)
    for (c = 1; c < 4; c++) {
      cblack_[c] = cblack_[0];
    }

  bool multiple_whites = false;
  int largest_white = this->get_white(0);

  for (c = 1; c < 4; c++) {
    if (this->get_white(c) != this->get_white(0)) {
      multiple_whites = true;

      if (this->get_white(c) > largest_white) {
        largest_white = this->get_white(c);
      }
    }
  }

  if (multiple_whites) {
    // dcraw's pre_mul/cam_mul expects a single white, so if we have provided multiple whites we need
    // to adapt scaling to avoid color shifts.
    for (c = 0; c < 4; c++) {
      // we don't really need to do the largest_white division but do so just to keep pre_mul in similar
      // range as before adjustment so they don't look strangely large if someone would print them
      pre_mul_[c] *= (float)this->get_white(c) / largest_white;
    }
  }

  for (dmin = DBL_MAX, dmax = c = 0; c < 4; c++) {
    if (dmin > pre_mul_[c]) {
      dmin = pre_mul_[c];
    }

    if (dmax < pre_mul_[c]) {
      dmax = pre_mul_[c];
    }
  }

  for (c = 0; c < 4; c++) {
    int sat = this->get_white(c) - cblack_[c];
    scale_mul_[c] = (pre_mul_[c] /= dmax) * 65535.0 / sat;
  }

  if (settings->verbose) {
    float asn[4] = { 1 / cam_mul[0], 1 / cam_mul[1], 1 / cam_mul[2], 1 / cam_mul[3] };

    for (dmax = c = 0; c < 4; c++) {
      if (cam_mul[c] == 0) {
        asn[c] = 0;
      }

      if (asn[c] > dmax) {
        dmax = asn[c];
      }
    }

    for (c = 0; c < 4; c++) {
      asn[c] /= dmax;
    }

    printf("cam_mul:[%f %f %f %f], AsShotNeutral:[%f %f %f %f]\n",
        cam_mul[0], cam_mul[1], cam_mul[2], cam_mul[3], asn[0], asn[1], asn[2], asn[3]);
    printf("pre_mul:[%f %f %f %f], scale_mul:[%f %f %f %f], cblack:[%f %f %f %f]\n",
        pre_mul_[0], pre_mul_[1], pre_mul_[2], pre_mul_[3],
        scale_mul_[0], scale_mul_[1], scale_mul_[2], scale_mul_[3],
        cblack_[0], cblack_[1], cblack_[2], cblack_[3]);
    printf("rgb_cam:[ [ %f %f %f], [%f %f %f], [%f %f %f] ]%s\n",
        rgb_cam[0][0], rgb_cam[1][0], rgb_cam[2][0],
        rgb_cam[0][1], rgb_cam[1][1], rgb_cam[2][1],
        rgb_cam[0][2], rgb_cam[1][2], rgb_cam[2][2],
        (!this->isBayer()) ? " (not bayer)" : "");

  }
}

int RawImage::loadRaw (bool loadData, unsigned int imageNum, bool closeFile, ProgressListener *plistener, double progressRange)
{
  ifname = filename.c_str();
  image = nullptr;
  verbose = settings->verbose;
  oprof = nullptr;

  if(!ifp) {
    ifp = gfopen (ifname);  // Maps to either file map or direct fopen
  } else  {
    fseek (ifp, 0, SEEK_SET);
  }

  if (!ifp) {
    return 3;
  }

  imfile_set_plistener(ifp, plistener, 0.9 * progressRange);

  char datadir[PATH_MAX] = { 0 }, camfile[PATH_MAX] = { 0 };
  //snprintf(camfile, sizeof(camfile), "%s/share/darktable/rawspeed/cameras.xml", DATA_SEARCH_PATH);
  meta = CameraDefinitions::getInstance()->getCameraMetaData();

  if( !meta ) return 3;



  char filen[PATH_MAX] = { 0 };
  snprintf(filen, sizeof(filen), "%s", ifname);
  FileReader f(filen);

  std::unique_ptr<const Buffer> m;

  dt_image_t img_;
  dt_image_t* img = &img_;

  try
  {
    m = std::unique_ptr<const Buffer>(f.readFile());

    RawParser t(m.get());
    decoder = std::unique_ptr<RawDecoder>(t.getDecoder(meta));

    if(!decoder.get()) return 3;

    decoder->failOnUnknown = true;
    decoder->checkSupport(meta);
    decoder->decodeRaw();
    decoder->decodeMetaData(meta);
    rawspeed::RawImage r = decoder->mRaw;

    const auto errors = r->getErrors();
    for(const auto &error : errors) fprintf(stderr, "[rawspeed] (%s) %s\n", img->filename, error.c_str());

    //g_strlcpy(make, r->metadata.make.c_str(), sizeof(make));
    //g_strlcpy(model, r->metadata.model.c_str(), sizeof(model));
    g_strlcpy(make, r->metadata.canonical_make.c_str(), sizeof(make));
    g_strlcpy(model, r->metadata.canonical_model.c_str(), sizeof(model));

    printf("RawImage::loadRaw(): make=\"%s\"  model=\"%s\"\n", make, model);

    shot_select = 0;
    is_raw = 1;


    /*
      g_strlcpy(img->camera_maker, r->metadata.canonical_make.c_str(), sizeof(img->camera_maker));
      g_strlcpy(img->camera_model, r->metadata.canonical_model.c_str(), sizeof(img->camera_model));
      g_strlcpy(img->camera_alias, r->metadata.canonical_alias.c_str(), sizeof(img->camera_alias));
      dt_image_refresh_makermodel(img);

      // We used to partial match the Canon local rebrandings so lets pass on
      // the value just in those cases to be able to fix old history stacks
      static const struct {
        const char *mungedname;
        const char *origname;
      } legacy_aliases[] = {
        {"Canon EOS","Canon EOS REBEL SL1"},
        {"Canon EOS","Canon EOS Kiss X7"},
        {"Canon EOS","Canon EOS DIGITAL REBEL XT"},
        {"Canon EOS","Canon EOS Kiss Digital N"},
        {"Canon EOS","Canon EOS 350D"},
        {"Canon EOS","Canon EOS DIGITAL REBEL XSi"},
        {"Canon EOS","Canon EOS Kiss Digital X2"},
        {"Canon EOS","Canon EOS Kiss X2"},
        {"Canon EOS","Canon EOS REBEL T5i"},
        {"Canon EOS","Canon EOS Kiss X7i"},
        {"Canon EOS","Canon EOS Rebel T6i"},
        {"Canon EOS","Canon EOS Kiss X8i"},
        {"Canon EOS","Canon EOS Rebel T6s"},
        {"Canon EOS","Canon EOS 8000D"},
        {"Canon EOS","Canon EOS REBEL T1i"},
        {"Canon EOS","Canon EOS Kiss X3"},
        {"Canon EOS","Canon EOS REBEL T2i"},
        {"Canon EOS","Canon EOS Kiss X4"},
        {"Canon EOS REBEL T3","Canon EOS REBEL T3i"},
        {"Canon EOS","Canon EOS Kiss X5"},
        {"Canon EOS","Canon EOS REBEL T4i"},
        {"Canon EOS","Canon EOS Kiss X6i"},
        {"Canon EOS","Canon EOS DIGITAL REBEL XS"},
        {"Canon EOS","Canon EOS Kiss Digital F"},
        {"Canon EOS","Canon EOS REBEL T5"},
        {"Canon EOS","Canon EOS Kiss X70"},
        {"Canon EOS","Canon EOS DIGITAL REBEL XTi"},
        {"Canon EOS","Canon EOS Kiss Digital X"},
      };

      for (uint32 i=0; i<(sizeof(legacy_aliases)/sizeof(legacy_aliases[1])); i++)
        if (!strcmp(legacy_aliases[i].origname, r->metadata.model.c_str())) {
          g_strlcpy(img->camera_legacy_makermodel, legacy_aliases[i].mungedname, sizeof(img->camera_legacy_makermodel));
          break;
        }
     */

    iso_speed = r->metadata.isoSpeed;
    aperture = 1.1;

    img->raw_black_level = r->blackLevel;
    img->raw_white_point = r->whitePoint;

    if(r->blackLevelSeparate[0] == -1 || r->blackLevelSeparate[1] == -1 || r->blackLevelSeparate[2] == -1
        || r->blackLevelSeparate[3] == -1)
    {
      r->calculateBlackAreas();
    }

    for(uint8_t i = 0; i < 4; i++) img->raw_black_level_separate[i] = r->blackLevelSeparate[i];

    if(r->blackLevel == -1)
    {
      float black = 0.0f;
      for(uint8_t i = 0; i < 4; i++)
      {
        black += img->raw_black_level_separate[i];
      }
      black /= 4.0f;

      img->raw_black_level = CLAMP(black, 0, UINT16_MAX);
    }

    /*
     * FIXME
     * if(r->whitePoint == 65536)
     *   ???
     */

    /* free auto pointers on spot */
    //d.reset();
    //m.reset();

    // Grab the WB
    for(int i = 0; i < 4; i++) img->wb_coeffs[i] = r->metadata.wbCoeffs[i];

    //img->buf_dsc.filters = 0u;
    if(!r->isCFA)
    {
      //dt_imageio_retval_t ret = dt_imageio_open_rawspeed_sraw(img, r, mbuf);
      return 3;
    }

    if((r->getDataType() != TYPE_USHORT16) && (r->getDataType() != TYPE_FLOAT32)) return 3;

    if((r->getBpp() != sizeof(uint16_t)) && (r->getBpp() != sizeof(float))) return 3;

    if((r->getDataType() == TYPE_USHORT16) && (r->getBpp() != sizeof(uint16_t))) return 3;

    if((r->getDataType() == TYPE_FLOAT32) && (r->getBpp() != sizeof(float))) return 3;

    printf("RawImage_RawSpeed::loadRaw(): r->getCpp()=%d\n", (int)r->getCpp());
    const float cpp = r->getCpp();
    if(cpp != 1) return 3;

    //img->buf_dsc.channels = 1;

    switch(r->getBpp())
    {
    case sizeof(uint16_t):
                  //img->buf_dsc.datatype = TYPE_UINT16;
                  break;
    case sizeof(float):
                  //img->buf_dsc.datatype = TYPE_FLOAT;
                  break;
    default:
      return 3;
      break;
    }

    // dimensions of uncropped image
    iPoint2D dimUncropped = r->getUncroppedDim();
    img->width = dimUncropped.x;
    img->height = dimUncropped.y;

    // dimensions of cropped image
    iPoint2D dimCropped = r->dim;

    // crop - Top,Left corner
    iPoint2D cropTL = r->getCropOffset();
    img->crop_x = cropTL.x;
    img->crop_y = cropTL.y;

    // crop - Bottom,Right corner
    iPoint2D cropBR = dimUncropped - dimCropped - cropTL;
    img->crop_width = dimCropped.x;//cropBR.x;
    img->crop_height = dimCropped.y;//cropBR.y;

    img->fuji_rotation_pos = r->metadata.fujiRotationPos;
    img->pixel_aspect_ratio = (float)r->metadata.pixelAspectRatio;

    filters = r->cfa.getDcrawFilter();

    printf("RawImage_RawSpeed::loadRaw(): width=%d height=%d  cropped x=%d y=%d w=%d h=%d\n",
        (int)dimUncropped.x, (int)dimUncropped.y, (int)cropTL.x, (int)cropTL.y, (int)cropBR.x, (int)cropBR.y);

    // special handling for x-trans sensors
    if(filters == 9u)
    {
      // get 6x6 CFA offset from top left of cropped image
      // NOTE: This is different from how things are done with Bayer
      // sensors. For these, the CFA in cameras.xml is pre-offset
      // depending on the distance modulo 2 between raw and usable
      // image data. For X-Trans, the CFA in cameras.xml is
      // (currently) aligned with the top left of the raw data.
      for(int i = 0; i < 6; ++i)
        for(int j = 0; j < 6; ++j)
        {
          xtrans[j][i] = r->cfa.getColorAt(i % 6, j % 6);
        }
    }

    flip = 0;
    if (flip == 5) {
      this->rotate_deg = 270;
    } else if (flip == 3) {
      this->rotate_deg = 180;
    } else if (flip == 6) {
      this->rotate_deg = 90;
    } else if (flip % 90 == 0 && flip < 360) {
      this->rotate_deg = flip;
    } else {
      this->rotate_deg = 0;
    }

    use_camera_wb = 1;
    shrink = 0;

    if (settings->verbose) {
      printf ("Loading %s %s image from %s...\n", make, model, filename.c_str());
    }

    width = img->crop_width;
    height = img->crop_height;
    iheight = height;
    iwidth  = width;

    printf("RawImage_RawSpeed::loadRaw(): iwidth=%d iheight=%d\n",iwidth,iheight);

    if (plistener) {
      plistener->setProgress(0.9 * progressRange);
    }

    CameraConstantsStore* ccs = CameraConstantsStore::getInstance();
    CameraConst *cc = ccs->get(make, model);

    printf("RawImage_RawSpeed::loadRaw(): cc=%p\n", (void*)cc);

    if (isBayer() || isXtrans()) {
      if (cc && cc->has_rawCrop()) {
        int lm, tm, w, h;
        cc->get_rawCrop(lm, tm, w, h);
        if(isXtrans()) {
          shiftXtransMatrix(6 - ((top_margin - tm)%6), 6 - ((left_margin - lm)%6));
        } else {
          if(((int)top_margin - tm) & 1) { // we have an odd border difference
            filters = (filters << 4) | (filters >> 28);    // left rotate filters by 4 bits
          }
        }
        left_margin = lm;
        top_margin = tm;

        if (w < 0) {
          iwidth += w;
          iwidth -= left_margin;
          width += w;
          width -= left_margin;
        } else if (w > 0) {
          iwidth = width = min((int)width, w);
        }

        if (h < 0) {
          iheight += h;
          iheight -= top_margin;
          height += h;
          height -= top_margin;
        } else if (h > 0) {
          iheight = height = min((int)height, h);
        }
      }

      if (cc && cc->has_rawMask(0)) {
        for (int i = 0; i < 8 && cc->has_rawMask(i); i++) {
          cc->get_rawMask(i, mask[i][0], mask[i][1], mask[i][2], mask[i][3]);
        }
      }

      //crop_masked_pixels();
    } else {
      if (get_maker() == "Sigma" && cc && cc->has_rawCrop()) { // foveon images
        int lm, tm, w, h;
        cc->get_rawCrop(lm, tm, w, h);
        left_margin = lm;
        top_margin = tm;

        if (w < 0) {
          width += w;
          width -= left_margin;
        } else if (w > 0) {
          width = min((int)width, w);
        }

        if (h < 0) {
          height += h;
          height -= top_margin;
        } else if (h > 0) {
          height = min((int)height, h);
        }
      }
    }

    /*
        // Load embedded profile
        if (profile_length) {
            profile_data = new char[profile_length];
            fseek ( ifp, profile_offset, SEEK_SET);
            fread ( profile_data, 1, profile_length, ifp);
        }
     */
    /*
          Setting the black level, there are three sources:
          dcraw single value 'black' or multi-value 'cblack', can be calculated or come
          from a hard-coded table or come from a stored value in the raw file, and
          finally there's 'black_c4' which are table values provided by RT camera constants.
          Any of these may or may not be set.

          We reduce these sources to one four channel black level, and do this by picking
          the highest found.
     */
    int black_c4[4] = { -1, -1, -1, -1 };

    bool white_from_cc = false;
    bool black_from_cc = false;
    tiff_bps = 0; // what is this used for???

    if (cc) {
      for (int i = 0; i < 4; i++) {
        if (RT_blacklevel_from_constant) {
          int blackFromCc = cc->get_BlackLevel(i, iso_speed);
          // if black level from camconst > 0xffff it is an absolute value.
          black_c4[i] = blackFromCc > 0xffff ? (blackFromCc & 0xffff) : blackFromCc + cblack[i];
        }

        // load 4 channel white level here, will be used if available
        printf("RawImage_RawSpeed::loadRaw(): white level from cc=%d  iso_speed=%f  aperture=%f\n",
            (int)cc->get_WhiteLevel(i, iso_speed, aperture), (float)iso_speed, (float)aperture);
        if (RT_whitelevel_from_constant) {
          maximum_c4[i] = cc->get_WhiteLevel(i, iso_speed, aperture);

          if(tiff_bps > 0 && maximum_c4[i] > 0 && !isFoveon()) {
            unsigned compare = ((uint64_t)1 << tiff_bps) - 1; // use uint64_t to avoid overflow if tiff_bps == 32

            while(static_cast<uint64_t>(maximum_c4[i]) > compare) {
              maximum_c4[i] >>= 1;
            }
          }
        }
      }
    }

    if (black_c4[0] == -1) {
      if(isXtrans())
        for (int c = 0; c < 4; c++) {
          black_c4[c] = cblack[6];
        }
      else

        // RT constants not set, bring in the DCRAW single channel black constant
        for (int c = 0; c < 4; c++) {
          black_c4[c] = black + cblack[c];
        }
    } else {
      black_from_cc = true;
    }

    if (maximum_c4[0] > 0) {
      white_from_cc = true;
    }

    for (int c = 0; c < 4; c++) {
      if (static_cast<int>(cblack[c]) < black_c4[c]) {
        cblack[c] = black_c4[c];
      }
    }

    /*
     * the copy of the RAW data is deferred to compress_image(),
     * since data is already loaded by RawSpeed
     */

    if (settings->verbose) {
      if (cc) {
        printf("constants exists for \"%s %s\" in camconst.json\n", make, model);
      } else {
        printf("no constants in camconst.json exists for \"%s %s\" (relying only on dcraw defaults)\n", make, model);
      }

      printf("black levels: R:%d G1:%d B:%d G2:%d (%s)\n", get_cblack(0), get_cblack(1), get_cblack(2), get_cblack(3),
          black_from_cc ? "provided by camconst.json" : "provided by dcraw");
      printf("white levels: R:%d G1:%d B:%d G2:%d (%s)\n", get_white(0), get_white(1), get_white(2), get_white(3),
          white_from_cc ? "provided by camconst.json" : "provided by dcraw");
      printf("raw crop: %d %d %d %d (provided by %s)\n", left_margin, top_margin, iwidth, iheight, (cc && cc->has_rawCrop()) ? "camconst.json" : "dcraw");
      printf("color matrix provided by %s\n", (cc && cc->has_dcrawMatrix()) ? "camconst.json" : "dcraw");
    }
  }
  catch(const std::exception &exc)
  {
    printf("[rawspeed] (%s) %s\n", img->filename, exc.what());

    /* if an exception is raised lets not retry or handle the
       specific ones, consider the file as corrupted */
    return 3;
  }
  catch(...)
  {
    printf("Unhandled exception in imageio_rawspeed\n");
    return 3;
  }


  if ( closeFile ) {
    fclose(ifp);
    ifp = nullptr;
  }

  if (plistener) {
    plistener->setProgress(1.0 * progressRange);
  }

  return 0;
}

float** RawImage::compress_image(int frameNum)
{
  rawspeed::RawImage r = decoder->mRaw;

  printf("RawImage_RawSpeed::compress_image(%d) called, w=%d h=%d\n", frameNum, width, height);

  if (isBayer() || isXtrans()) {
    if (!allocation) {
      // shift the beginning of all frames but the first by 32 floats to avoid cache miss conflicts on CPUs which have <= 4-way associative L1-Cache
      allocation = new float[height * width + frameNum * 32];
      data = new float*[height];

      for (int i = 0; i < height; i++) {
        data[i] = allocation + i * width + frameNum * 32;
      }
    }
  } else if (colors == 1) {
    // Monochrome
    if (!allocation) {
      allocation = new float[height * width];
      data = new float*[height];

      for (int i = 0; i < height; i++) {
        data[i] = allocation + i * width;
      }
    }
  } else {
    if (!allocation) {
      allocation = new float[3 * height * width];
      data = new float*[height];

      for (int i = 0; i < height; i++) {
        data[i] = allocation + 3 * i * width;
      }
    }
  }

  int col2, row2;
#pragma omp parallel for

  for (int row = 0; row < height; row++)
    for (int col = 0; col < width; col++) {
      col2 = col + left_margin;
      row2 = row + top_margin;
      float val = 0;
      switch(r->getDataType()) {
      case rawspeed::TYPE_USHORT16: val = *((uint16_t*)r->getDataUncropped(col2,row2)); break;
      case rawspeed::TYPE_FLOAT32: val = *((float*)r->getDataUncropped(col2,row2)); break;
      }
      if(true && row<4 && col<4) {
        printf("  raw(%d,%d): %f  color=%d\n",row,col,val,(int)FC(row, col));
      }
      this->data[row][col] = val;
    }

  decoder.reset();

  image = nullptr;
  return data;
}

bool
RawImage::is_supportedThumb() const
{
  return false;
  /*
    return ( (thumb_width * thumb_height) > 0 &&
             ( write_thumb == &rtengine::RawImage_RawSpeed::jpeg_thumb ||
               write_thumb == &rtengine::RawImage_RawSpeed::ppm_thumb) &&
             !thumb_load_raw );
   */
}

bool
RawImage::is_jpegThumb() const
{
  return false;
  /*
    return ( (thumb_width * thumb_height) > 0 &&
              write_thumb == &rtengine::RawImage_RawSpeed::jpeg_thumb &&
             !thumb_load_raw );
   */
}

bool
RawImage::is_ppmThumb() const
{
  return false;
  /*
    return ( (thumb_width * thumb_height) > 0 &&
             write_thumb == &rtengine::RawImage_RawSpeed::ppm_thumb &&
             !thumb_load_raw );
   */
}

void RawImage::getXtransMatrix( int XtransMatrix[6][6])
{
  for(int row = 0; row < 6; row++)
    for(int col = 0; col < 6; col++) {
      XtransMatrix[row][col] = xtrans[row][col];
    }
}

void RawImage::getRgbCam (float rgbcam[3][4])
{
  for(int row = 0; row < 3; row++)
    for(int col = 0; col < 4; col++) {
      rgbcam[row][col] = rgb_cam[row][col];
    }

}

bool
RawImage::get_thumbSwap() const
{
  return (order == 0x4949) == (ntohs(0x1234) == 0x1234);
}

} //namespace rtengine
