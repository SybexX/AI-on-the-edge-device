#include "CAlignAndCutImage.h"
#include "ClassControllCamera.h"
#include "CRotateImage.h"
#include "ClassLogFile.h"

#include <math.h>
#include <algorithm>
#include <esp_log.h>
#include "psram.h"
#include "defines.h"
#include "Helper.h"

static const char *TAG = "c_align_and_cut_image";

CAlignAndCutImage::CAlignAndCutImage(std::string _name, CImageBasis *_org, CImageBasis *_temp) : CImageBasis(_name)
{
    name = _name;
    rgb_image = _org->rgb_image;
    channels = _org->channels;
    width = _org->width;
    height = _org->height;
    bpp = _org->bpp;
    externalImage = true;
    islocked = false;
    ImageTMP = _temp;
}

int CAlignAndCutImage::AlignImage(RefInfo *_temp1, RefInfo *_temp2)
{
    CFindTemplate *ft = new CFindTemplate("align", rgb_image, channels, width, height, bpp);

    //////////////////////////////////////////////
    ESP_LOGD(TAG, "AlignImage: before ft->FindTemplate(_temp1); %s", _temp1->image_file.c_str());
    bool isSimilar1 = ft->FindTemplate(_temp1); // search the alignment image 1

    _temp1->width = ft->tpl_width;
    _temp1->height = ft->tpl_height;

    int x1_relative_shift = (_temp1->target_x - _temp1->found_x);
    int y1_relative_shift = (_temp1->target_y - _temp1->found_y);

    int x1_absolute_shift = _temp1->target_x + (_temp1->target_x - _temp1->found_x);
    int y1_absolute_shift = _temp1->target_y + (_temp1->target_y - _temp1->found_y);

    ESP_LOGD(TAG, "***************************************************");
    ESP_LOGD(TAG, "alignment image 1:");
    ESP_LOGD(TAG, "AlignImage: _temp1->target_x(x): %d - _temp1->target_y(y): %d", _temp1->target_x, _temp1->target_y);
    ESP_LOGD(TAG, "AlignImage: _temp1->width(dx): %d - _temp1->height(dy): %d", _temp1->width, _temp1->height);
    ESP_LOGD(TAG, "AlignImage: _temp1->found_x: %d - _temp1->found_y: %d", _temp1->found_x, _temp1->found_y);
    ESP_LOGD(TAG, "AlignImage: x1_relative_shift: %d - y1_relative_shift: %d", x1_relative_shift, y1_relative_shift);
    ESP_LOGD(TAG, "AlignImage: x1_absolute_shift: %d - y1_absolute_shift: %d", x1_absolute_shift, y1_absolute_shift);

    //////////////////////////////////////////////
    ESP_LOGD(TAG, "AlignImage: before ft->FindTemplate(_temp2); %s", _temp2->image_file.c_str());
    bool isSimilar2 = ft->FindTemplate(_temp2); // search the alignment image 2

    _temp2->width = ft->tpl_width;
    _temp2->height = ft->tpl_height;

    int x2_relative_shift = (_temp2->target_x - _temp2->found_x);
    int y2_relative_shift = (_temp2->target_y - _temp2->found_y);

    int x2_absolute_shift = _temp2->target_x + (_temp2->target_x - _temp2->found_x);
    int y2_absolute_shift = _temp2->target_y + (_temp2->target_y - _temp2->found_y);

    ESP_LOGD(TAG, "***************************************************");
    ESP_LOGD(TAG, "alignment image 2:");
    ESP_LOGD(TAG, "AlignImage: _temp2->target_x(x): %d - _temp2->target_y(y): %d", _temp2->target_x, _temp2->target_y);
    ESP_LOGD(TAG, "AlignImage: _temp2->width(dx): %d - _temp2->height(dy): %d", _temp2->width, _temp2->height);
    ESP_LOGD(TAG, "AlignImage: _temp2->found_x: %d - _temp2->found_y: %d", _temp2->found_x, _temp2->found_y);
    ESP_LOGD(TAG, "AlignImage: x2_relative_shift: %d - y2_relative_shift: %d", x2_relative_shift, y2_relative_shift);
    ESP_LOGD(TAG, "AlignImage: x2_absolute_shift: %d - y2_absolute_shift: %d", x2_absolute_shift, y2_absolute_shift);

    //////////////////////////////////////////////
    delete ft;

    //////////////////////////////////////////////
    float radians_org = atan2((_temp2->found_y - _temp1->found_y), (_temp2->found_x - _temp1->found_x));
    float radians_cur = atan2((y2_absolute_shift - y1_absolute_shift), (x2_absolute_shift - x1_absolute_shift));
    float rotate_angle = (radians_cur - radians_org) * 180 / M_PI; // radians to degrees

    ESP_LOGD(TAG, "***************************************************");
    ESP_LOGD(TAG, "alignment angle:");
    ESP_LOGD(TAG, "AlignImage: _temp1->search_max_angle: %f - _temp2->search_max_angle: %f", _temp1->search_max_angle, _temp2->search_max_angle);
    ESP_LOGD(TAG, "AlignImage: radians_org: %f - radians_cur: %f", radians_org, radians_cur);
    ESP_LOGD(TAG, "AlignImage: angle_org: %f - angle_cur: %f - rotate_angle: %f", (radians_org * 180 / M_PI), (radians_cur * 180 / M_PI), rotate_angle);
    ESP_LOGD(TAG, "***************************************************");

    //////////////////////////////////////////////
    int ret = Alignment_OK;
    if ((fabs(rotate_angle) > _temp1->search_max_angle) || (fabs(rotate_angle) > _temp2->search_max_angle))
    {
        ret = Alignment_Failed;
        ESP_LOGE(TAG, "Alignment failed: image rotation outside the set range - angle: %f", rotate_angle);
    }

    if ((abs(x1_relative_shift) >= _temp1->search_x) || (abs(y1_relative_shift) >= _temp1->search_y))
    {
        ret |= Alignment_Failed;
        ESP_LOGE(TAG, "Alignment failed: image shift outside the set range - x1-shift: %d - y1-shift: %d - x2-shift: %d - y2-shift: %d", x1_relative_shift, y1_relative_shift, x2_relative_shift, y2_relative_shift);
    }

    //////////////////////////////////////////////
    CRotateImage rt("Align", this, ImageTMP);

    if (rotate_angle != 0)
    {
        ESP_LOGD(TAG, "Align: Image shifting and rotate");
        rt.TranslateImage(x1_relative_shift, y1_relative_shift);

        if (CameraConfigSaved.ImageAntialiasing)
        {
            rt.RotateAntiAliasingImage(rotate_angle, _temp1->target_x, _temp1->target_y);
        }
        else
        {
            rt.RotateImage(rotate_angle, _temp1->target_x, _temp1->target_y);
        }
    }
    else if (x1_relative_shift != 0 || y1_relative_shift != 0)
    {
        ESP_LOGD(TAG, "Align: Image shifting in the X-axis and y-axis");
        rt.TranslateImage(x1_relative_shift, y1_relative_shift);
    }

    ESP_LOGD(TAG, "AlignImage: x1_relative_shift %d - y1_relative_shift %d - x2_relative_shift %d - y2_relative_shift %d - rotation_angle %f", x1_relative_shift, y1_relative_shift, x2_relative_shift, y2_relative_shift, rotate_angle);

    if (ret == Alignment_Failed)
    {
        return ret;
    }
    else
    {
        return (isSimilar1 && isSimilar2);
    }
}

void CAlignAndCutImage::CutAndSave(std::string _template1, int x1, int y1, int dx, int dy)
{
    int x2 = x1 + dx;
    int y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    int memsize = dx * dy * channels;
    uint8_t *odata = (unsigned char *)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);

    stbi_uc *p_target;
    stbi_uc *p_source;

    RGBImageLock();

    for (int x = x1; x < x2; ++x)
    {
        for (int y = y1; y < y2; ++y)
        {
            p_target = odata + (channels * ((y - y1) * dx + (x - x1)));
            p_source = rgb_image + (channels * (y * width + x));
            for (int _channels = 0; _channels < channels; ++_channels)
            {
                p_target[_channels] = p_source[_channels];
            }
        }
    }

#ifdef STBI_ONLY_JPEG
    stbi_write_jpg(_template1.c_str(), dx, dy, channels, odata, 100);
#else
    stbi_write_bmp(_template1.c_str(), dx, dy, channels, odata);
#endif

    RGBImageRelease();

    stbi_image_free(odata);
}

void CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy, CImageBasis *_target)
{
    int x2 = x1 + dx;
    int y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    if ((_target->height != dy) || (_target->width != dx) || (_target->channels != channels))
    {
        ESP_LOGD(TAG, "CAlignAndCutImage::CutAndSave - Image size does not match!");
        return;
    }

    uint8_t *odata = _target->RGBImageLock();
    RGBImageLock();

    stbi_uc *p_target;
    stbi_uc *p_source;

    for (int x = x1; x < x2; ++x)
    {
        for (int y = y1; y < y2; ++y)
        {
            p_target = odata + (channels * ((y - y1) * dx + (x - x1)));
            p_source = rgb_image + (channels * (y * width + x));
            for (int _channels = 0; _channels < channels; ++_channels)
            {
                p_target[_channels] = p_source[_channels];
            }
        }
    }

    RGBImageRelease();
    _target->RGBImageRelease();
}

CImageBasis *CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy)
{
    int x2 = x1 + dx;
    int y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    int memsize = dx * dy * channels;
    uint8_t *odata = (unsigned char *)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);

    stbi_uc *p_target;
    stbi_uc *p_source;

    RGBImageLock();

    for (int x = x1; x < x2; ++x)
    {
        for (int y = y1; y < y2; ++y)
        {
            p_target = odata + (channels * ((y - y1) * dx + (x - x1)));
            p_source = rgb_image + (channels * (y * width + x));
            for (int _channels = 0; _channels < channels; ++_channels)
            {
                p_target[_channels] = p_source[_channels];
            }
        }
    }

    CImageBasis *rs = new CImageBasis("CutAndSave", odata, channels, dx, dy, bpp);
    RGBImageRelease();
    rs->SetIndepended();
    return rs;
}
