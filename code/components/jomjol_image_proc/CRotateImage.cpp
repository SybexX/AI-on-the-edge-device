#include <string>
#include "CRotateImage.h"
#include "psram.h"

static const char *TAG = "C ROTATE IMG";

int pixel_fill_color = 216; // Gray

CRotateImage::CRotateImage(std::string _name, CImageBasis *_org, CImageBasis *_temp, bool _flip) : CImageBasis(_name)
{
    rgb_image = _org->rgb_image;
    channels = _org->channels;
    width = _org->width;
    height = _org->height;
    bpp = _org->bpp;
    externalImage = true;   
    ImageTMP = _temp;   
    ImageOrg = _org; 
    islocked = false;
    doflip = _flip;
}

void CRotateImage::Rotate(float _angle, int _centerx, int _centery)
{
    float radians = (float)(_angle / 180 * M_PI);

    float cos_angle = cos(radians);
    float sin_angle = sin(radians);

    float x_rot = ((float)_centerx * (1 - cos_angle)) - ((float)_centery * sin_angle);
    float y_rot = ((float)_centerx * sin_angle) + ((float)_centery * (1 - cos_angle));

    int memsize = width * height * channels;
    uint8_t *odata;

    if (ImageTMP) {
        odata = ImageTMP->RGBImageLock();
    }
    else {
        odata = (unsigned char *)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);
    }

    int x_source, y_source;
    stbi_uc *p_target;
    stbi_uc *p_source;

    RGBImageLock();

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            p_target = odata + (channels * ((y * width) + x));

            x_source = int((x * cos_angle) + (y * sin_angle) + x_rot);
            y_source = int(-(x * sin_angle) + (y * cos_angle) + y_rot);

            if ((x_source >= 0) && (x_source < width) && (y_source >= 0) && (y_source < height)) {
                p_source = rgb_image + (channels * ((y_source * width) + x_source));

                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = p_source[_channels];
                }
            }
            else {
                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = pixel_fill_color;
                }
            }
        }
    }

    memCopy(odata, rgb_image, memsize);

    if (ImageTMP) {
        ImageTMP->RGBImageRelease();
    }
    else {
        free_psram_heap(std::string(TAG) + "->odata", odata);
    }

    RGBImageRelease();
}

void CRotateImage::RotateAntiAliasing(float _angle, int _centerx, int _centery)
{
    float radians = float(_angle / 180 * M_PI);

    float cos_angle = cos(radians);
    float sin_angle = sin(radians);

    float x_rot = ((float)_centerx * (1 - cos_angle)) - ((float)_centery * sin_angle);
    float y_rot = ((float)_centerx * sin_angle) + ((float)_centery * (1 - cos_angle));

    int memsize = width * height * channels;
    uint8_t *odata;

    if (ImageTMP) {
        odata = ImageTMP->RGBImageLock();
    }
    else {
        odata = (unsigned char *)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);
    }

    int x_source_1, y_source_1, x_source_2, y_source_2;
    float x_source, y_source;
    float quad_ul, quad_ur, quad_ol, quad_or;
    stbi_uc *p_target;
    stbi_uc *p_source_ul, *p_source_ur, *p_source_ol, *p_source_or;

    RGBImageLock();

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            p_target = odata + (channels * ((y * width) + x));

            x_source = int((x * cos_angle) + (y * sin_angle) + x_rot);
            y_source = int(-(x * sin_angle) + (y * cos_angle) + y_rot);

            x_source_1 = (int)x_source;
            x_source_2 = x_source_1 + 1;

            y_source_1 = (int)y_source;
            y_source_2 = y_source_1 + 1;

            quad_ul = (x_source_2 - x_source) * (y_source_2 - y_source);
            quad_ur = (1 - (x_source_2 - x_source)) * (y_source_2 - y_source);

            quad_or = (x_source_2 - x_source) * (1 - (y_source_2 - y_source));
            quad_ol = (1 - (x_source_2 - x_source)) * (1 - (y_source_2 - y_source));

            if ((x_source_1 >= 0) && (x_source_2 < width) && (y_source_1 >= 0) && (y_source_2 < height)) {
                p_source_ul = rgb_image + (channels * ((y_source_1 * width) + x_source_1));
                p_source_ur = rgb_image + (channels * ((y_source_1 * width) + x_source_2));

                p_source_or = rgb_image + (channels * ((y_source_2 * width) + x_source_1));
                p_source_ol = rgb_image + (channels * ((y_source_2 * width) + x_source_2));

                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = (int)(((float)p_source_ul[_channels] * quad_ul) + ((float)p_source_ur[_channels] * quad_ur) + ((float)p_source_or[_channels] * quad_or) + ((float)p_source_ol[_channels] * quad_ol));
                }
            }
            else {
                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = pixel_fill_color;
                }
            }
        }
    }

    memCopy(odata, rgb_image, memsize);

    if (ImageTMP) {
        ImageTMP->RGBImageRelease();
    }
    else {
        free_psram_heap(std::string(TAG) + "->odata", odata);
    }

    RGBImageRelease();
}

void CRotateImage::Rotate(float _angle)
{
//    ESP_LOGD(TAG, "width %d, height %d", width, height);
    Rotate(_angle, width / 2, height / 2);
}

void CRotateImage::RotateAntiAliasing(float _angle)
{
//    ESP_LOGD(TAG, "width %d, height %d", width, height);
    RotateAntiAliasing(_angle, width / 2, height / 2);
}

void CRotateImage::Translate(int _dx, int _dy)
{
    int memsize = width * height * channels;
    uint8_t *odata;

    if (ImageTMP) {
        odata = ImageTMP->RGBImageLock();
    }
    else {
        odata = (unsigned char *)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);
    }

    int x_source, y_source;
    stbi_uc *p_target;
    stbi_uc *p_source;

    RGBImageLock();

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            p_target = odata + (channels * ((y * width) + x));

            x_source = x - _dx;
            y_source = y - _dy;

            if ((x_source >= 0) && (x_source < width) && (y_source >= 0) && (y_source < height)) {
                p_source = rgb_image + (channels * ((y_source * width) + x_source));

                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = p_source[_channels];
                }
            }
            else {
                for (int _channels = 0; _channels < channels; ++_channels) {
                    p_target[_channels] = pixel_fill_color;
                }
            }
        }
    }

    memCopy(odata, rgb_image, memsize);

    if (ImageTMP) {
        ImageTMP->RGBImageRelease();
    }
    else {
        free_psram_heap(std::string(TAG) + "->odata", odata);
    }

    RGBImageRelease();
}
