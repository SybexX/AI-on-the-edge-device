#include "CAlignAndCutImage.h"
#include "CRotateImage.h"
#include "ClassLogFile.h"

#include <math.h>
#include <algorithm>
#include <cstring>
#include <stdint.h>
#include <limits.h>
#include <new>
#include <esp_log.h>
#include "psram.h"

#include "../../include/defines.h"
#include "Helper.h"

static const char* TAG = "c_align_and_cut_image";

class ScopedImageAccess
{
    CImageBasis* image;
    bool locked;
public:
    explicit ScopedImageAccess(CImageBasis* value) : image(value), locked(false)
    {
        if (image != NULL) locked = image->RGBImageLock() != NULL;
    }
    ~ScopedImageAccess()
    {
        if (locked) image->RGBImageRelease();
    }
    bool isLocked() const { return locked; }
};

static bool getCutRectangle(int imageWidth, int imageHeight,
                             int x, int y, int requestedWidth, int requestedHeight,
                             int* right, int* bottom)
{
    if (right == NULL || bottom == NULL || imageWidth <= 0 || imageHeight <= 0 ||
        x < 0 || y < 0 || requestedWidth <= 0 || requestedHeight <= 0 ||
        x >= imageWidth || y >= imageHeight) return false;

    const int64_t requestedRight = static_cast<int64_t>(x) + requestedWidth;
    const int64_t requestedBottom = static_cast<int64_t>(y) + requestedHeight;
    *right = static_cast<int>(std::min<int64_t>(requestedRight, imageWidth));
    *bottom = static_cast<int>(std::min<int64_t>(requestedBottom, imageHeight));
    return *right > x && *bottom > y;
}

static bool getCutBufferSize(int width, int height, int channels, size_t* size)
{
    if (size == NULL || width <= 0 || height <= 0 || channels <= 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    const size_t c = static_cast<size_t>(channels);
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / c) return false;
    const size_t total = w * h * c;
    if (total > static_cast<size_t>(INT_MAX)) return false;
    *size = total;
    return true;
}

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


bool CAlignAndCutImage::Align(RefInfo *_temp1, RefInfo *_temp2)
{
    int dx, dy;
    int r0_x, r0_y, r1_x, r1_y;
    bool isSimilar1, isSimilar2;

    if (_temp1 == NULL || _temp2 == NULL || rgb_image == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Align: source image or reference is unavailable");
        return false;
    }
    ScopedImageAccess sourceAccess(this);
    if (!sourceAccess.isLocked()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Align: unable to lock source image");
        return false;
    }
    CFindTemplate* ft = new (std::nothrow) CFindTemplate("align", rgb_image, channels, width, height, bpp);
    if (ft == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Align: unable to allocate template matcher");
        return false;
    }

    r0_x = _temp1->target_x;
    r0_y = _temp1->target_y;
    ESP_LOGD(TAG, "Before ft->FindTemplate(_temp1); %s", _temp1->image_file.c_str());
    isSimilar1 = ft->FindTemplate(_temp1);
    _temp1->width = ft->tpl_width;
    _temp1->height = ft->tpl_height; 

    r1_x = _temp2->target_x;
    r1_y = _temp2->target_y;
    ESP_LOGD(TAG, "Before ft->FindTemplate(_temp2); %s", _temp2->image_file.c_str());
    isSimilar2 = ft->FindTemplate(_temp2);
    _temp2->width = ft->tpl_width;
    _temp2->height = ft->tpl_height; 

    delete ft;

    dx = _temp1->target_x - _temp1->found_x;
    dy = _temp1->target_y - _temp1->found_y;

    r0_x += dx;
    r0_y += dy;

    r1_x += dx;
    r1_y += dy;

    float w_org, w_ist, d_winkel;

	w_org = atan2((float)(_temp2->found_y - _temp1->found_y), (float)(_temp2->found_x - _temp1->found_x));
    w_ist = atan2((float)(r1_y - r0_y), (float)(r1_x - r0_x));

    d_winkel = (w_ist - w_org) * 180 / M_PI;

/*#ifdef DEBUG_DETAIL_ON
    std::string zw = "\tdx:\t" + std::to_string(dx) + "\tdy:\t" + std::to_string(dy) + "\td_winkel:\t" + std::to_string(d_winkel);
    zw = zw + "\tt1_x_y:\t" + std::to_string(_temp1->found_x) + "\t" + std::to_string(_temp1->found_y);
    zw = zw + "\tpara1_found_min_avg_max_SAD:\t" + std::to_string(_temp1->fastalg_min) + "\t" + std::to_string(_temp1->fastalg_avg) + "\t" + std::to_string(_temp1->fastalg_max) + "\t"+ std::to_string(_temp1->fastalg_SAD);
    zw = zw + "\tt2_x_y:\t" + std::to_string(_temp2->found_x) + "\t" + std::to_string(_temp2->found_y);
    zw = zw + "\tpara2_found_min_avg_max:\t" + std::to_string(_temp2->fastalg_min) + "\t" + std::to_string(_temp2->fastalg_avg) + "\t" + std::to_string(_temp2->fastalg_max) + "\t"+ std::to_string(_temp2->fastalg_SAD);
    LogFile.WriteToDedicatedFile("/sdcard/alignment.txt", zw);
#endif*/

    CRotateImage rt("Align", this, ImageTMP);
    rt.Translate(dx, dy);
    rt.Rotate(d_winkel, _temp1->target_x, _temp1->target_y);
    ESP_LOGD(TAG, "Alignment: dx %d - dy %d - rot %f", dx, dy, d_winkel);

    return (isSimilar1 && isSimilar2);
}

void CAlignAndCutImage::CutAndSave(std::string _template1, int x1, int y1, int dx, int dy)
{
    if (rgb_image == NULL || channels <= 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: source image is not available");
        return;
    }

    int x2 = 0, y2 = 0;
    if (!getCutRectangle(width, height, x1, y1, dx, dy, &x2, &y2)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: invalid crop rectangle");
        return;
    }
    const int cropWidth = x2 - x1;
    const int cropHeight = y2 - y1;
    size_t bufferSize = 0;
    if (!getCutBufferSize(cropWidth, cropHeight, channels, &bufferSize)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: crop size overflow");
        return;
    }

    uint8_t* source = RGBImageLock();
    if (source == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: source image is locked");
        return;
    }
    uint8_t* output = static_cast<uint8_t*>(malloc_psram_heap(
        std::string(TAG) + "->odata", bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (output == NULL) {
        RGBImageRelease();
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: PSRAM allocation failed");
        return;
    }

    const size_t rowBytes = static_cast<size_t>(cropWidth) * static_cast<size_t>(channels);
    for (int row = 0; row < cropHeight; ++row) {
        const size_t sourceOffset = (static_cast<size_t>(y1 + row) * static_cast<size_t>(width) +
                                     static_cast<size_t>(x1)) * static_cast<size_t>(channels);
        std::memcpy(output + static_cast<size_t>(row) * rowBytes, source + sourceOffset, rowBytes);
    }

#ifdef STBI_ONLY_JPEG
    stbi_write_jpg(_template1.c_str(), cropWidth, cropHeight, channels, output, 100);
#else
    stbi_write_bmp(_template1.c_str(), cropWidth, cropHeight, channels, output);
#endif

    RGBImageRelease();
    free_psram_heap(std::string(TAG) + "->odata", output);
}

bool CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy, CImageBasis *_target)
{
    if (_target == NULL || rgb_image == NULL || channels <= 0) return false;

    int x2 = 0, y2 = 0;
    if (!getCutRectangle(width, height, x1, y1, dx, dy, &x2, &y2)) return false;
    const int cropWidth = x2 - x1;
    const int cropHeight = y2 - y1;
    size_t cropBufferSize = 0;
    if (!getCutBufferSize(cropWidth, cropHeight, channels, &cropBufferSize)) return false;
    if (_target->height != cropHeight || _target->width != cropWidth ||
        _target->channels != channels || !_target->ImageOkay()) {
        ESP_LOGD(TAG, "CAlignAndCutImage::CutAndSave - Image size does not match!");
        return false;
    }

    uint8_t* output = _target->RGBImageLock();
    if (output == NULL) return false;
    uint8_t* source = RGBImageLock();
    if (source == NULL) {
        _target->RGBImageRelease();
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(cropWidth) * static_cast<size_t>(channels);
    for (int row = 0; row < cropHeight; ++row) {
        const size_t sourceOffset = (static_cast<size_t>(y1 + row) * static_cast<size_t>(width) +
                                     static_cast<size_t>(x1)) * static_cast<size_t>(channels);
        std::memcpy(output + static_cast<size_t>(row) * rowBytes, source + sourceOffset, rowBytes);
    }

    RGBImageRelease();
    _target->RGBImageRelease();
    return true;
}

CImageBasis* CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy)
{
    if (rgb_image == NULL || channels <= 0) return NULL;
    int x2 = 0, y2 = 0;
    if (!getCutRectangle(width, height, x1, y1, dx, dy, &x2, &y2)) return NULL;
    const int cropWidth = x2 - x1;
    const int cropHeight = y2 - y1;
    size_t bufferSize = 0;
    if (!getCutBufferSize(cropWidth, cropHeight, channels, &bufferSize)) return NULL;

    uint8_t* source = RGBImageLock();
    if (source == NULL) return NULL;
    uint8_t* output = static_cast<uint8_t*>(malloc_psram_heap(
        std::string(TAG) + "->odata", bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (output == NULL) {
        RGBImageRelease();
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CutAndSave: PSRAM allocation failed");
        return NULL;
    }

    const size_t rowBytes = static_cast<size_t>(cropWidth) * static_cast<size_t>(channels);
    for (int row = 0; row < cropHeight; ++row) {
        const size_t sourceOffset = (static_cast<size_t>(y1 + row) * static_cast<size_t>(width) +
                                     static_cast<size_t>(x1)) * static_cast<size_t>(channels);
        std::memcpy(output + static_cast<size_t>(row) * rowBytes, source + sourceOffset, rowBytes);
    }
    RGBImageRelease();

    CImageBasis* result = new (std::nothrow) CImageBasis("CutAndSave", output, channels, cropWidth, cropHeight, bpp);
    if (result == NULL) {
        free_psram_heap(std::string(TAG) + "->odata (unowned)", output);
        return NULL;
    }
    if (!result->SetIndepended()) {
        delete result;
        free_psram_heap(std::string(TAG) + "->odata (invalid result)", output);
        return NULL;
    }
    // SetIndepended copied the externally-owned crop into its own managed PSRAM block.
    free_psram_heap(std::string(TAG) + "->odata (transferred)", output);
    return result;
}
