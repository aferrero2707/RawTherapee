/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __RAWIMAGE_RAWSPEED_H
#define __RAWIMAGE_RAWSPEED_H

#include <ctime>

#include "RawSpeed-API.h"


namespace rtengine
{

struct badPix {
    uint16_t x;
    uint16_t y;
    badPix( uint16_t xc, uint16_t yc ): x(xc), y(yc) {}
};

class PixelsMap :
    public NonCopyable
{
    int w; // line width in base_t units
    int h; // height
    typedef unsigned long base_t;
    static const size_t base_t_size = sizeof(base_t);
    base_t *pm;

public:
    PixelsMap(int width, int height )
        : h(height)
    {
        w = (width / (base_t_size * 8)) + 1;
        pm = new base_t [h * w ];
        memset(pm, 0, h * w * base_t_size );
    }

    ~PixelsMap()
    {
        delete [] pm;
    }
    int width() const
    {
        return w;
    }
    int height() const
    {
        return h;
    }

    // if a pixel is set returns true
    bool get(int x, int y)
    {
        return (pm[y * w + x / (base_t_size * 8) ] & (base_t)1 << (x % (base_t_size * 8)) ) != 0;
    }

    // set a pixel
    void set(int x, int y)
    {
        pm[y * w + x / (base_t_size * 8) ] |= (base_t)1 << (x % (base_t_size * 8)) ;
    }

    // set pixels from a list
    int set( std::vector<badPix> &bp)
    {
        for(std::vector<badPix>::iterator iter = bp.begin(); iter != bp.end(); ++iter) {
            set( iter->x, iter->y);
        }

        return bp.size();
    }

    void clear()
    {
        memset(pm, 0, h * w * base_t_size );
    }
    // return 0 if at least one pixel in the word(base_t) is set, otherwise return the number of pixels to skip to the next word base_t
    int skipIfZero(int x, int y)
    {
        return pm[y * w + x / (base_t_size * 8) ] == 0 ? base_t_size * 8 - x % (base_t_size * 8) : 0;
    }
};



class RawImage
{
public:

  typedef unsigned short (*dcrawImage_t)[4];
    explicit RawImage( const Glib::ustring &name );
    ~RawImage();

    int loadRaw (bool loadData, unsigned int imageNum = 0, bool closeFile = true, ProgressListener *plistener = nullptr, double progressRange = 1.0);
    void get_colorsCoeff( float* pre_mul_, float* scale_mul_, float* cblack_, bool forceAutoWB );
    void set_prefilters()
    {
        if (isBayer() && get_colors() == 3) {
            prefilters = filters;
            filters &= ~((filters & 0x55555555) << 1);
        }
    }
    dcrawImage_t get_image()
    {
        return image;
    }
    float** compress_image(int frameNum); // revert to compressed pixels format and release image data
    float** data;             // holds pixel values, data[i][j] corresponds to the ith row and jth column
    unsigned filters;               // modified filters for 3 color processing?
    unsigned prefilters;               // original filters saved ( used for 4 color processing )
    unsigned int getFrameCount() const { return is_raw; }
protected:
    int verbose, use_auto_wb, use_camera_wb, use_camera_matrix;
    int exif_base, ciff_base, ciff_len;
    IMFILE *ifp;
    FILE *ofp;
    std::unique_ptr<rawspeed::RawDecoder> decoder;
    short order;
    const char *ifname;
    int mask[8][4], flip, tiff_flip, colors;
    unsigned is_raw, dng_version, is_foveon;
    float cam_mul[4], pre_mul[4], cmatrix[3][4], rgb_cam[3][4];
    int xtrans[6][6],xtrans_abs[6][6];
    char cdesc[5], desc[512], make[64], model[64], model2[64], model3[64], artist[64];
    float flash_used, canon_ev, iso_speed, shutter, aperture, focal_len;
    time_t timestamp;
    off_t    strip_offset, data_offset;
    off_t    thumb_offset, meta_offset, profile_offset;
    unsigned thumb_length, meta_length, profile_length;
    unsigned thumb_misc, *oprof, fuji_layout, shot_select, multi_out;
    ushort raw_height, raw_width, height, width, top_margin, left_margin;
    ushort shrink, iheight, iwidth, fuji_width, thumb_width, thumb_height;
    unsigned tiff_nifds, tiff_samples, tiff_bps, tiff_compress;
    unsigned black, cblack[4102], maximum, mix_green, raw_color, zero_is_bad;
    ushort white[8][8], curve[0x10000], cr2_slice[3], sraw_mul[4];
    unsigned raw_size;
    ushort *raw_image;
    float * float_raw_image;
    dcrawImage_t image;
    int RT_whitelevel_from_constant;
    int RT_blacklevel_from_constant;
    int RT_matrix_from_constant;

    Glib::ustring filename; // complete filename
    int rotate_deg; // 0,90,180,270 degree of rotation: info taken by dcraw from exif
    char* profile_data; // Embedded ICC color profile
    float* allocation; // pointer to allocated memory
    int maximum_c4[4];
    rawspeed::CameraMetaData *meta;

    bool isFoveon() const
    {
        return is_foveon;
    }

    void shiftXtransMatrix( const int offsy, const int offsx) {
        char xtransTemp[6][6];
        for(int row = 0;row < 6; row++) {
            for(int col = 0;col < 6; col++) {
                xtransTemp[row][col] = xtrans[(row+offsy)%6][(col+offsx)%6];
            }
        }
        for(int row = 0;row < 6; row++) {
            for(int col = 0;col < 6; col++) {
                xtrans[row][col] = xtransTemp[row][col];
            }
        }
    }

public:

    static void initCameraConstants(Glib::ustring baseDir);
    std::string get_filename() const
    {
        return filename;
    }
    int get_width()  const
    {
        return width;
    }
    int get_height() const
    {
        return height;
    }
    int get_iwidth()  const
    {
        return iwidth;
    }
    int get_iheight()  const
    {
        return iheight;
    }
    int get_leftmargin()  const
    {
        return left_margin;
    }
    int get_topmargin() const
    {
        return top_margin;
    }
    int get_FujiWidth() const
    {
        return fuji_width;
    }
    eSensorType getSensorType();

    void getRgbCam (float rgbcam[3][4]);
    void getXtransMatrix ( int xtransMatrix[6][6]);
    unsigned get_filters() const
    {
        return filters;
    }
    int get_colors() const
    {
        return colors;
    }
    int get_cblack(int i) const
    {
        return cblack[i];
    }
    int get_white(int i) const
    {
        if (maximum_c4[0] > 0) {
            return maximum_c4[i];
        } else {
            return maximum;
        }
    }
    unsigned short get_whiteSample( int r, int c ) const
    {
        return white[r][c];
    }

    double get_ISOspeed() const
    {
        return iso_speed;
    }
    double get_shutter()  const
    {
        return shutter;
    }
    double get_aperture()  const
    {
        return aperture;
    }
    time_t get_timestamp() const
    {
        return timestamp;
    }
    int get_rotateDegree() const
    {
        return rotate_deg;
    }
    const std::string get_maker() const
    {
        return std::string(make);
    }
    const std::string get_model() const
    {
        return std::string(model);
    }

    float get_cam_mul(int c )const
    {
        return cam_mul[c];
    }
    float get_pre_mul(int c )const
    {
        return pre_mul[c];
    }
    float get_rgb_cam( int r, int c) const
    {
        return rgb_cam[r][c];
    }

    int get_exifBase()  const
    {
        return exif_base;
    }
    int get_ciffBase() const
    {
        return ciff_base;
    }
    int get_ciffLen()  const
    {
        return ciff_len;
    }

    int get_profileLen() const
    {
        return profile_length;
    }
    char* get_profile() const
    {
        return profile_data;
    }
    IMFILE *get_file() const
    {
        return ifp;
    }
    bool is_supportedThumb() const ;
    bool is_jpegThumb() const ;
    bool is_ppmThumb() const ;
    int get_thumbOffset() const
    {
        return int(thumb_offset);
    }
    int get_thumbWidth() const
    {
        return int(thumb_width);
    }
    int get_thumbHeight() const
    {
        return int(thumb_height);
    }
    int get_thumbBPS() const
    {
      return 8;
        //return thumb_load_raw ? 16 : 8;
    }
    bool get_thumbSwap() const;
    unsigned get_thumbLength() const
    {
        return thumb_length;
    }
    bool zeroIsBad() const
    {
        return zero_is_bad == 1;
    }

    bool isBayer() const
    {
        return (filters != 0 && filters != 9);
    }

    bool isXtrans() const
    {
        return filters == 9;
    }

public:
    bool ISRED  (unsigned row, unsigned col) const
    {
        return ((filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3) == 0);
    }
    bool ISGREEN(unsigned row, unsigned col) const
    {
        return ((filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3) == 1);
    }
    bool ISBLUE (unsigned row, unsigned col) const
    {
        return ((filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3) == 2);
    }
    unsigned FC (unsigned row, unsigned col) const
    {
        return (filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3);
    }
    bool ISXTRANSRED  (unsigned row, unsigned col) const
    {
        return ((xtrans[(row) % 6][(col) % 6]) == 0);
    }
    bool ISXTRANSGREEN(unsigned row, unsigned col) const
    {
        return ((xtrans[(row) % 6][(col) % 6]) == 1);
    }
    bool ISXTRANSBLUE (unsigned row, unsigned col) const
    {
        return ((xtrans[(row) % 6][(col) % 6]) == 2);
    }
    unsigned XTRANSFC (unsigned row, unsigned col) const
    {
        return (xtrans[(row) % 6][(col) % 6]);
    }

    unsigned DNGVERSION ( ) const
    {
        return dng_version;
    }
};

}

#endif // __RAWIMAGE_RAWSPEED_H
