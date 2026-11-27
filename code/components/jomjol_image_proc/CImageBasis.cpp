#include "CImageBasis.h"
#include "Helper.h"
#include "psram.h"
#include "ClassLogFile.h"
#include "server_ota.h"

#include <esp_log.h>
#include "../../include/defines.h"

#include "esp_system.h"

#include <cstring>
#include <limits.h>

#include <math.h>
#include <algorithm>


using namespace std;

static const char *TAG = "C IMG BASIS";

static bool getImageBufferSize(int width, int height, int channels, size_t* size)
{
    if (size == nullptr || width <= 0 || height <= 0 || channels <= 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    const size_t c = static_cast<size_t>(channels);
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / c) return false;
    const size_t total = w * h * c;
    if (total > static_cast<size_t>(INT_MAX)) return false;
    *size = total;
    return true;
}

static void releaseOwnedImageBuffer(const std::string& tag, const std::string& name,
                                    uint8_t* image, int size, bool sharedLease)
{
    if (image == nullptr) return;
    if (sharedLease) psram_free_shared_temp_image_memory();
    else if (size == 0) stbi_image_free(image);
    else free_psram_heap(tag + " (" + name + ", " + std::to_string(size) + ")", image);
}



//#define DEBUG_DETAIL_ON


uint8_t * CImageBasis::RGBImageLock(int _waitmaxsec)
{
    if (islocked)
    {
        #ifdef DEBUG_DETAIL_ON   
                ESP_LOGD(TAG, "Image is locked: sleep for: %ds", _waitmaxsec);
        #endif
        TickType_t xDelay;
        xDelay = 1000 / portTICK_PERIOD_MS;
        for (int i = 0; i <= _waitmaxsec; ++i)
        {
            vTaskDelay( xDelay ); 
            if (!islocked)
                break;
        }
    }

    if (islocked)
        return NULL;

    return rgb_image;
}


void CImageBasis::RGBImageRelease()
{
    islocked = false;
}


uint8_t * CImageBasis::RGBImageGet()
{
    return rgb_image;
}

bool CImageBasis::SetIndepended()
{
    size_t size = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &size)) return false;
    externalImage = false;
    memsize = static_cast<int>(size);
    ownsSharedTempImageLease = false;
    return true;
}


void writejpghelp(void *context, void *data, int size)
{
    ImageData* output = static_cast<ImageData*>(context);
    if (output == nullptr || data == nullptr || size <= 0 || output->size > MAX_JPG_SIZE) return;
    if (static_cast<size_t>(size) > (MAX_JPG_SIZE - output->size)) {
        output->size = MAX_JPG_SIZE + 1U;
        return;
    }
    std::memcpy(output->data + output->size, data, static_cast<size_t>(size));
    output->size += static_cast<size_t>(size);
}


ImageData* CImageBasis::writeToMemoryAsJPG(const int quality)
{
    ImageData* ii = new (std::nothrow) ImageData;
    if (ii == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "writeToMemoryAsJPG: PSRAM allocation failed");
        return nullptr;
    }
    writeToMemoryAsJPG(ii, quality);
    return ii;
}


void CImageBasis::writeToMemoryAsJPG(ImageData* i, const int quality)
{
    if (i == nullptr) return;
    i->size = 0;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "writeToMemoryAsJPG: image is unavailable or locked");
        return;
    }
    const int success = stbi_write_jpg_to_func(writejpghelp, i, width, height, channels, image, quality);
    RGBImageRelease();
    if (i->size > MAX_JPG_SIZE) {
        i->size = 0;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "writeToMemoryAsJPG: Creation aborted! JPG size > preallocated buffer: " + std::to_string(MAX_JPG_SIZE));
    } else if (!success) {
        i->size = 0;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "writeToMemoryAsJPG: JPEG encoder failed");
    }
}


struct SendJPGHTTP
{
    httpd_req_t *req;
    esp_err_t res;
    char *buf;
    size_t size = 0;
};
static void flushJPGHTTPBuffer(SendJPGHTTP* send)
{
    if (send == nullptr || send->res != ESP_OK || send->size == 0) return;
    if (httpd_resp_send_chunk(send->req, send->buf, send->size) != ESP_OK) {
        ESP_LOGE(TAG, "File sending failed!");
        send->res = ESP_FAIL;
    }
    send->size = 0;
}
inline void writejpgtohttphelp(void *context, void *data, int size)
{
    SendJPGHTTP* send = static_cast<SendJPGHTTP*>(context);
    if (send == nullptr || data == nullptr || size <= 0 || send->res != ESP_OK) return;
    const uint8_t* source = static_cast<const uint8_t*>(data);
    size_t remaining = static_cast<size_t>(size);
    while (remaining > 0 && send->res == ESP_OK) {
        if (send->size >= HTTP_BUFFER_SENT) {
            flushJPGHTTPBuffer(send);
            if (send->res != ESP_OK) return;
        }
        const size_t available = HTTP_BUFFER_SENT - send->size;
        const size_t copySize = std::min(available, remaining);
        std::memcpy(send->buf + send->size, source, copySize);
        send->size += copySize;
        source += copySize;
        remaining -= copySize;
    }
}
esp_err_t CImageBasis::SendJPGtoHTTP(httpd_req_t *_req, const int quality)
{
    if (_req == nullptr) return ESP_FAIL;
    SendJPGHTTP send;
    send.req = _req; send.res = ESP_OK; send.size = 0;
    send.buf = static_cast<char*>(malloc_psram_heap("JPEG HTTP stream buffer", HTTP_BUFFER_SENT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (send.buf == nullptr) return ESP_ERR_NO_MEM;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) {
        free_psram_heap("JPEG HTTP stream buffer", send.buf);
        return ESP_FAIL;
    }
    const int encoded = stbi_write_jpg_to_func(writejpgtohttphelp, &send, width, height, channels, image, quality);
    if (!encoded && send.res == ESP_OK) send.res = ESP_FAIL;
    flushJPGHTTPBuffer(&send);
    RGBImageRelease();
    free_psram_heap("JPEG HTTP stream buffer", send.buf);
    return send.res;
}


bool CImageBasis::CopyFromMemory(uint8_t* _source, int _size)
{
    size_t expected = 0;
    if (_source == nullptr || rgb_image == nullptr || _size < 0 ||
        !getImageBufferSize(width, height, channels, &expected) ||
        expected != static_cast<size_t>(_size)) {
        ESP_LOGE(TAG, "Cannot copy image from memory - sizes do not match: should be %u, but is %d",
                 static_cast<unsigned int>(expected), _size);
        return false;
    }
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return false;
    memCopy(_source, image, _size);
    RGBImageRelease();
    return true;
}


uint8_t CImageBasis::GetPixelColor(int x, int y, int ch)
{
    size_t imageSize = 0;
    if (rgb_image == nullptr || !isInImage(x, y) || ch < 0 || ch >= channels ||
        !getImageBufferSize(width, height, channels, &imageSize)) return 0;
    const size_t offset = static_cast<size_t>(channels) *
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x));
    if (offset > imageSize || static_cast<size_t>(ch) >= imageSize - offset) return 0;
    return rgb_image[offset + static_cast<size_t>(ch)];
}


void CImageBasis::memCopy(uint8_t* _source, uint8_t* _target, int _size)
{
#ifdef _ESP32_PSRAM
    for (int i = 0; i < _size; ++i)
        *(_target + i) = *(_source + i);
#else
    memcpy(_target, _source, _size);
#endif
}


bool CImageBasis::isInImage(int x, int y)
{
    if ((x < 0) || (x > width - 1))
        return false;

    if ((y < 0) || (y > height- 1))
        return false;

    return true;
}


void CImageBasis::setPixelColor(int x, int y, int r, int g, int b)
{
    size_t imageSize = 0;
    if (rgb_image == nullptr || !isInImage(x, y) ||
        !getImageBufferSize(width, height, channels, &imageSize)) return;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return;
    const size_t offset = static_cast<size_t>(channels) *
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x));
    if (offset >= imageSize) { RGBImageRelease(); return; }
    uint8_t* pixel = image + offset;
    pixel[0] = static_cast<uint8_t>(r);
    if (channels > 2) { pixel[1] = static_cast<uint8_t>(g); pixel[2] = static_cast<uint8_t>(b); }
    RGBImageRelease();
}


void CImageBasis::drawRect(int x, int y, int dx, int dy, int r, int g, int b, int thickness)
{
    int zwx1, zwx2, zwy1, zwy2;
    int _x, _y, _thick;

    zwx1 = x - thickness + 1;
    zwx2 = x + dx + thickness - 1;
    zwy1 = y;
    zwy2 = y;

    RGBImageLock();

    for (_thick = 0; _thick < thickness; _thick++)
        for (_x = zwx1; _x <= zwx2; ++_x)
            for (_y = zwy1; _y <= zwy2; _y++)
                if (isInImage(_x, _y))
                    setPixelColor(_x, _y - _thick, r, g, b);

    zwx1 = x - thickness + 1;
    zwx2 = x + dx + thickness - 1;
    zwy1 = y + dy;
    zwy2 = y + dy;
    for (_thick = 0; _thick < thickness; _thick++)
        for (_x = zwx1; _x <= zwx2; ++_x)
            for (_y = zwy1; _y <= zwy2; _y++)
                if (isInImage(_x, _y))
                    setPixelColor(_x, _y + _thick, r, g, b);

    zwx1 = x;
    zwx2 = x;
    zwy1 = y;
    zwy2 = y + dy;
    for (_thick = 0; _thick < thickness; _thick++)
        for (_x = zwx1; _x <= zwx2; ++_x)
            for (_y = zwy1; _y <= zwy2; _y++)
                if (isInImage(_x, _y))
                    setPixelColor(_x - _thick, _y, r, g, b);

    zwx1 = x + dx;
    zwx2 = x + dx;
    zwy1 = y;
    zwy2 = y + dy;
    for (_thick = 0; _thick < thickness; _thick++)
        for (_x = zwx1; _x <= zwx2; ++_x)
            for (_y = zwy1; _y <= zwy2; _y++)
                if (isInImage(_x, _y))
                    setPixelColor(_x + _thick, _y, r, g, b);

    RGBImageRelease();
}


void CImageBasis::drawLine(int x1, int y1, int x2, int y2, int r, int g, int b, int thickness)
{
    int _x, _y, _thick;
    int _zwy1, _zwy2;
    thickness = (thickness-1) / 2;

    RGBImageLock();

    for (_thick = 0; _thick <= thickness; ++_thick)
        for (_x = x1 - _thick; _x <= x2 + _thick; ++_x)
        {
            if (x2 == x1)
            {
                _zwy1 = y1;
                _zwy2 = y2;
            }
            else
            {
                _zwy1 = (y2 - y1) * (float)(_x - x1) / (float)(x2 - x1) + y1;
                _zwy2 = (y2 - y1) * (float)(_x + 1 - x1) / (float)(x2 - x1) + y1;
            }

            for (_y = _zwy1 - _thick; _y <= _zwy2 + _thick; _y++)
                if (isInImage(_x, _y))
                    setPixelColor(_x, _y, r, g, b);
        }
    
    RGBImageRelease();
}


void CImageBasis::drawEllipse(int x1, int y1, int radx, int rady, int r, int g, int b, int thickness)
{
    float deltarad, aktrad;
    int _thick, _x, _y;
    int rad = radx;

    if (rady > radx)
        rad = rady;

    deltarad = 1 / (4 * M_PI * (rad + thickness - 1));

    RGBImageLock();

    for (aktrad = 0; aktrad <= (2 * M_PI); aktrad += deltarad)
        for (_thick = 0; _thick < thickness; ++_thick)
        {
            _x = sin(aktrad) * (radx + _thick) + x1;
            _y = cos(aktrad) * (rady + _thick) + y1;
            if (isInImage(_x, _y))
                setPixelColor(_x, _y, r, g, b);
        }

    RGBImageRelease();
}


void CImageBasis::drawCircle(int x1, int y1, int rad, int r, int g, int b, int thickness)
{
    float deltarad, aktrad;
    int _thick, _x, _y;

    deltarad = 1 / (4 * M_PI * (rad + thickness - 1));

    RGBImageLock();

    for (aktrad = 0; aktrad <= (2 * M_PI); aktrad += deltarad)
        for (_thick = 0; _thick < thickness; ++_thick)
        {
            _x = sin(aktrad) * (rad + _thick) + x1;
            _y = cos(aktrad) * (rad + _thick) + y1;
            if (isInImage(_x, _y))
                setPixelColor(_x, _y, r, g, b);
        }

    RGBImageRelease();
}


CImageBasis::CImageBasis(string _name)
{
    name = _name;
    externalImage = false;
    rgb_image = NULL;
    width = 0;
    height = 0;
    channels = 0;    
    islocked = false;
}


void CImageBasis::CreateEmptyImage(int _width, int _height, int _channels)
{
    if (_width <= 0 || _height <= 0 || _channels <= 0 || name == "tmpImage") return;
    const size_t w = static_cast<size_t>(_width), h = static_cast<size_t>(_height), c = static_cast<size_t>(_channels);
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / c) return;
    const size_t newSize = w * h * c;
    if (newSize > INT_MAX) return;
    uint8_t* newImage = static_cast<uint8_t*>(malloc_psram_heap(
        std::string(TAG) + "->CImageBasis (" + name + ")", newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (newImage == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CreateEmptyImage: allocation failed");
        LogFile.WriteHeapInfo("CreateEmptyImage");
        return;
    }
    if (rgb_image != nullptr && !externalImage) {
        if (memsize == 0) {
            stbi_image_free(rgb_image);
        } else {
            free_psram_heap(std::string(TAG) + "->CImageBasis (" + name + ", " + std::to_string(memsize) + ")", rgb_image);
        }
    }
    rgb_image = newImage;
    width = _width; height = _height; channels = _channels; bpp = _channels;
    memsize = static_cast<int>(newSize);
    externalImage = false;
    std::memset(rgb_image, 0, newSize);
}


void CImageBasis::EmptyImage()
{
    size_t size = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &size)) return;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return;
    std::memset(image, 0, size);
    RGBImageRelease();
}


void CImageBasis::LoadFromMemory(stbi_uc *_buffer, int len)
{
    if (_buffer == NULL || len <= 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadFromMemory: invalid input buffer");
        return;
    }

    int newWidth = 0, newHeight = 0, sourceChannels = 0;
    uint8_t* decoded = stbi_load_from_memory(_buffer, len, &newWidth, &newHeight,
                                              &sourceChannels, STBI_rgb);
    size_t decodedSize = 0;
    if (decoded == NULL || !getImageBufferSize(newWidth, newHeight, STBI_rgb, &decodedSize)) {
        if (decoded != NULL) stbi_image_free(decoded);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "LoadFromMemory: image decode failed or returned invalid dimensions");
        LogFile.WriteHeapInfo("LoadFromMemory");
        return;
    }

    if (rgb_image != NULL && !externalImage) {
        releaseOwnedImageBuffer(std::string(TAG) + "->CImageBasis", name,
                                rgb_image, memsize, ownsSharedTempImageLease);
    }
    rgb_image = decoded;
    width = newWidth;
    height = newHeight;
    channels = STBI_rgb;
    bpp = sourceChannels;
    memsize = 0;
    externalImage = false;
    ownsSharedTempImageLease = false;
    ESP_LOGD(TAG, "Image loaded from memory: %d, %d, %d", width, height, channels);
}


void CImageBasis::crop_image(unsigned short cropLeft, unsigned short cropRight, unsigned short cropTop, unsigned short cropBottom)
{
    size_t inputSize = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &inputSize) ||
        static_cast<unsigned int>(cropLeft) + cropRight >= static_cast<unsigned int>(width) ||
        static_cast<unsigned int>(cropTop) + cropBottom >= static_cast<unsigned int>(height)) return;

    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(channels);
    const size_t firstRow = static_cast<size_t>(cropTop) * rowBytes;
    const size_t endRow = static_cast<size_t>(height - cropBottom) * rowBytes;
    const unsigned int maxX = static_cast<unsigned int>(width) - cropRight;
    const unsigned int newWidth = static_cast<unsigned int>(width) - cropLeft - cropRight;
    const unsigned int newHeight = static_cast<unsigned int>(height) - cropTop - cropBottom;
    size_t writeIndex = 0;

    for (size_t i = 0; i < inputSize; i += static_cast<size_t>(channels)) {
        const unsigned int x = static_cast<unsigned int>((i / static_cast<size_t>(channels)) % static_cast<size_t>(width));
        if (i < firstRow || i >= endRow || x < cropLeft || x >= maxX) continue;
        for (int channel = 0; channel < channels; ++channel) {
            if (i + static_cast<size_t>(channel) >= inputSize) return;
            rgb_image[writeIndex++] = rgb_image[i + static_cast<size_t>(channel)];
        }
    }
    width = static_cast<int>(newWidth);
    height = static_cast<int>(newHeight);
}


CImageBasis::CImageBasis(string _name, CImageBasis *_copyfrom)
{
    name = _name;
    islocked = false;
    externalImage = false;
    rgb_image = NULL;
    width = height = channels = bpp = 0;
    memsize = 0;
    ownsSharedTempImageLease = false;

    if (_copyfrom == NULL || !_copyfrom->ImageOkay()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-Copyfrom: source image is not available");
        return;
    }

    size_t imageSize = 0;
    if (!getImageBufferSize(_copyfrom->width, _copyfrom->height, _copyfrom->channels, &imageSize)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-Copyfrom: invalid source dimensions");
        return;
    }
    width = _copyfrom->width;
    height = _copyfrom->height;
    channels = _copyfrom->channels;
    bpp = _copyfrom->bpp;

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CImageBasis_copyfrom - Start");
#endif

    uint8_t* source = _copyfrom->RGBImageLock();
    if (source == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-Copyfrom: source image is locked");
        return;
    }

    if (name == "tmpImage") {
        rgb_image = static_cast<uint8_t*>(psram_reserve_shared_tmp_image_memory());
        ownsSharedTempImageLease = (rgb_image != NULL);
        memsize = (rgb_image != NULL) ? static_cast<int>(imageSize) : 0;
    } else {
        rgb_image = static_cast<uint8_t*>(malloc_psram_heap(
            std::string(TAG) + "->CImageBasis (" + name + ")", imageSize,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        memsize = (rgb_image != NULL) ? static_cast<int>(imageSize) : 0;
    }

    if (rgb_image == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-Copyfrom: PSRAM allocation failed (" + std::to_string(imageSize) + " bytes)");
        LogFile.WriteHeapInfo("CImageBasis-Copyfrom");
        _copyfrom->RGBImageRelease();
        return;
    }

    memCopy(source, rgb_image, static_cast<int>(imageSize));
    _copyfrom->RGBImageRelease();

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CImageBasis_copyfrom - done");
#endif
}


CImageBasis::CImageBasis(string _name, int _width, int _height, int _channels)
{
    name = _name;
    islocked = false;
    externalImage = false;
    ownsSharedTempImageLease = false;
    rgb_image = NULL;
    width = height = channels = bpp = 0;
    memsize = 0;

    size_t imageSize = 0;
    if (!getImageBufferSize(_width, _height, _channels, &imageSize)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-width,height,ch: invalid image dimensions");
        return;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CImageBasis_width,height,ch - Start");
#endif

    rgb_image = static_cast<uint8_t*>(malloc_psram_heap(
        std::string(TAG) + "->CImageBasis (" + name + ")", imageSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (rgb_image == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-width,height,ch: PSRAM allocation failed (" + std::to_string(imageSize) + " bytes)");
        LogFile.WriteHeapInfo("CImageBasis-width,height,ch");
        return;
    }

    width = _width;
    height = _height;
    channels = _channels;
    bpp = _channels;
    memsize = static_cast<int>(imageSize);

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CImageBasis_width,height,ch - done");
#endif
}


CImageBasis::CImageBasis(string _name, std::string _image)
{
    name = _name;
    islocked = false;
    channels = 3;
    externalImage = false;
    filename = _image;

    if (file_size(_image.c_str()) == 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, _image + " is empty!");
        return;
    }

    RGBImageLock();

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("CImageBasis_image - Start");
    #endif

    rgb_image = stbi_load(_image.c_str(), &width, &height, &bpp, channels);

    if (rgb_image == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CImageBasis-image: Failed to load " + _image + "! Is it corrupted?");
        LogFile.WriteHeapInfo("CImageBasis-image");
        RGBImageRelease();
        return;
    }
    
    RGBImageRelease();

    #ifdef DEBUG_DETAIL_ON 
        std::string zw = "CImageBasis after load " + _image;
        ESP_LOGD(TAG, "%s", zw.c_str());
        ESP_LOGD(TAG, "w %d, h %d, b %d, c %d", width, height, bpp, channels);
    #endif

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("CImageBasis_image - done");
    #endif
}


bool CImageBasis::ImageOkay(){
    size_t imageSize = 0;
    return rgb_image != NULL && getImageBufferSize(width, height, channels, &imageSize);
}


CImageBasis::CImageBasis(string _name, uint8_t* _rgb_image, int _channels, int _width, int _height, int _bpp)
{
    name = _name;
    islocked = false;
    rgb_image = _rgb_image;
    channels = _channels;
    width = _width;
    height = _height;
    bpp = _bpp;
    externalImage = true;
}


void CImageBasis::Negative(void)
{
    size_t size = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &size)) return;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return;
    for (size_t i = 0; i < size; i += static_cast<size_t>(channels))
        for (int c = 0; c < channels; ++c) image[i + static_cast<size_t>(c)] = 255 - image[i + static_cast<size_t>(c)];
    RGBImageRelease();
}


void CImageBasis::Contrast(float _contrast)
{
    size_t imageSize = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &imageSize)) return;
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return;
    const float contrast = (_contrast / 100) + 1;
    const float intercept = 128 * (1 - contrast);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = static_cast<size_t>(channels) *
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x));
            uint8_t* pixel = image + offset;
            for (int channel = 0; channel < channels; ++channel)
                pixel[channel] = static_cast<uint8_t>(std::min(255, std::max(0,
                    static_cast<int>(pixel[channel] * contrast + intercept))));
        }
    }
    RGBImageRelease();
}


CImageBasis::~CImageBasis()
{
    if (!externalImage && rgb_image != NULL) {
        releaseOwnedImageBuffer(std::string(TAG) + "->CImageBasis", name,
                                rgb_image, memsize, ownsSharedTempImageLease);
    }
    rgb_image = NULL;
    ownsSharedTempImageLease = false;
}


void CImageBasis::SaveToFile(std::string _imageout)
{
    size_t imageSize = 0;
    if (rgb_image == nullptr || !getImageBufferSize(width, height, channels, &imageSize)) return;
    const string typ = getFileType(_imageout);
    uint8_t* image = RGBImageLock();
    if (image == nullptr) return;
    if ((typ == "jpg") || (typ == "JPG")) stbi_write_jpg(_imageout.c_str(), width, height, channels, image, 0);
#ifndef STBI_ONLY_JPEG
    if ((typ == "bmp") || (typ == "BMP")) stbi_write_bmp(_imageout.c_str(), width, height, channels, image);
#endif
    RGBImageRelease();
}


void CImageBasis::Resize(int _new_dx, int _new_dy)
{
    if (rgb_image == NULL || name == "tmpImage") {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Resize: not possible for image " + name);
        return;
    }
    size_t newSize = 0;
    if (!getImageBufferSize(_new_dx, _new_dy, channels, &newSize)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Resize: invalid target dimensions");
        return;
    }

    uint8_t* source = RGBImageLock();
    if (source == NULL) return;
    uint8_t* output = static_cast<uint8_t*>(malloc_psram_heap(
        std::string(TAG) + "->CImageBasis Resize (" + name + ")", newSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (output == NULL) {
        RGBImageRelease();
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Resize: PSRAM allocation failed: " + std::to_string(newSize));
        return;
    }

    const int resized = stbir_resize_uint8(source, width, height, 0,
                                            output, _new_dx, _new_dy, 0, channels);
    RGBImageRelease();
    if (resized == 0) {
        free_psram_heap(std::string(TAG) + "->CImageBasis Resize (failed)", output);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Resize: stb resize failed");
        return;
    }

    if (!externalImage) {
        releaseOwnedImageBuffer(std::string(TAG) + "->CImageBasis Resize old", name,
                                rgb_image, memsize, ownsSharedTempImageLease);
    }
    rgb_image = output;
    memsize = static_cast<int>(newSize);
    externalImage = false;
    ownsSharedTempImageLease = false;
    width = _new_dx;
    height = _new_dy;
}


void CImageBasis::Resize(int _new_dx, int _new_dy, CImageBasis *_target)
{
    if (_target == NULL || rgb_image == NULL || !_target->ImageOkay() ||
        _target->height != _new_dy || _target->width != _new_dx ||
        _target->channels != channels) {
        ESP_LOGE(TAG, "Resize - Target image size does not fit or is unavailable!");
        return;
    }

    size_t sourceSize = 0, targetSize = 0;
    if (!getImageBufferSize(width, height, channels, &sourceSize) ||
        !getImageBufferSize(_new_dx, _new_dy, channels, &targetSize)) {
        ESP_LOGE(TAG, "Resize - Invalid source or target dimensions");
        return;
    }

    uint8_t* source = RGBImageLock();
    if (source == NULL) return;
    uint8_t* target = _target->RGBImageLock();
    if (target == NULL) {
        RGBImageRelease();
        return;
    }

    const int resized = stbir_resize_uint8(source, width, height, 0,
                                            target, _new_dx, _new_dy, 0, channels);
    _target->RGBImageRelease();
    RGBImageRelease();
    if (resized == 0) ESP_LOGE(TAG, "Resize - stb resize failed");
}

