#include "CFindTemplate.h"

#include "ClassLogFile.h"
#include "Helper.h"
#include "../../include/defines.h"

#include <esp_log.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <limits.h>

static const char* TAG = "C FIND TEMPL";

static bool getTemplateBufferSize(int width, int height, int channels, size_t* size)
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

// #define DEBUG_DETAIL_ON  


bool CFindTemplate::FindTemplate(RefInfo *_ref)
{
    if (_ref == NULL || rgb_image == NULL || width <= 0 || height <= 0 || channels <= 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "FindTemplate: source image or reference is unavailable");
        return false;
    }
    if (file_size(_ref->image_file.c_str()) == 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, _ref->image_file + " is empty!");
        return false;
    }

    uint8_t* rgb_template = stbi_load(_ref->image_file.c_str(), &tpl_width, &tpl_height, &tpl_bpp, channels);
    if (rgb_template == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to load " + _ref->image_file + "! Is it corrupted?");
        return false;
    }

    size_t templateBytes = 0;
    if (!getTemplateBufferSize(tpl_width, tpl_height, channels, &templateBytes) ||
        tpl_width > width || tpl_height > height) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "FindTemplate: template dimensions are invalid or exceed source image");
        stbi_image_free(rgb_template);
        return false;
    }

    uint8_t* source = RGBImageLock();
    if (source == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "FindTemplate: source image is locked");
        stbi_image_free(rgb_template);
        return false;
    }

    if (_ref->search_x == 0) {
        _ref->search_x = width;
        _ref->found_x = 0;
    }
    if (_ref->search_y == 0) {
        _ref->search_y = height;
        _ref->found_y = 0;
    }

    const int64_t leftCandidate = static_cast<int64_t>(_ref->target_x) - _ref->search_x;
    const int64_t rightCandidate = static_cast<int64_t>(_ref->target_x) + _ref->search_x;
    const int64_t topCandidate = static_cast<int64_t>(_ref->target_y) - _ref->search_y;
    const int64_t bottomCandidate = static_cast<int64_t>(_ref->target_y) + _ref->search_y;
    const int ow_start = static_cast<int>(std::max<int64_t>(0, leftCandidate));
    const int ow_stop = static_cast<int>(std::min<int64_t>(rightCandidate, width - tpl_width));
    const int oh_start = static_cast<int>(std::max<int64_t>(0, topCandidate));
    const int oh_stop = static_cast<int>(std::min<int64_t>(bottomCandidate, height - tpl_height));

    if (ow_stop < ow_start || oh_stop < oh_start) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "FindTemplate: search window does not intersect source image");
        RGBImageRelease();
        stbi_image_free(rgb_template);
        return false;
    }

    float avg = 0, SAD = 0;
    int min = 0, max = 0;
    bool isSimilar = false;
    if ((_ref->alignment_algo == 2) && (_ref->fastalg_x > -1) && (_ref->fastalg_y > -1)) {
        isSimilar = CalculateSimularities(rgb_template, _ref->fastalg_x, _ref->fastalg_y,
                                          min, avg, max, SAD, _ref->fastalg_SAD,
                                          _ref->fastalg_SAD_criteria);
    }
    if (isSimilar) {
#ifdef DEBUG_DETAIL_ON
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Use FastAlignment successful");
#endif
        _ref->found_x = _ref->fastalg_x;
        _ref->found_y = _ref->fastalg_y;
        RGBImageRelease();
        stbi_image_free(rgb_template);
        return true;
    }

    uint64_t aktSAD;
    uint64_t minSAD = UINT64_MAX;
    const int anzChannels = (_ref->alignment_algo == 0) ? 1 : channels;
    const size_t templateRowLength = static_cast<size_t>(tpl_width) * static_cast<size_t>(channels);
    int64_t nextYieldUs = esp_timer_get_time() + 50000;

    for (int xouter = ow_start; xouter <= ow_stop; ++xouter) {
        for (int youter = oh_start; youter <= oh_stop; ++youter) {
            aktSAD = 0;
            for (int tpl_y = 0; tpl_y < tpl_height; ++tpl_y) {
                const stbi_uc* p_org = source +
                    (static_cast<size_t>(youter + tpl_y) * static_cast<size_t>(width) +
                     static_cast<size_t>(xouter)) * static_cast<size_t>(channels);
                const stbi_uc* p_tpl = rgb_template + static_cast<size_t>(tpl_y) * templateRowLength;
                uint64_t rowSAD = 0;
                for (size_t tpl_x = 0; tpl_x < templateRowLength; tpl_x += static_cast<size_t>(channels)) {
                    for (int ch = 0; ch < anzChannels; ++ch) {
                        const int diff = static_cast<int>(p_tpl[tpl_x + static_cast<size_t>(ch)]) -
                                         static_cast<int>(p_org[tpl_x + static_cast<size_t>(ch)]);
                        rowSAD += static_cast<uint64_t>(diff * diff);
                    }
                }
                aktSAD += rowSAD;
                if (aktSAD >= minSAD) break;
            }
            if (aktSAD < minSAD) {
                minSAD = aktSAD;
                _ref->found_x = xouter;
                _ref->found_y = youter;
            }
            if (esp_timer_get_time() >= nextYieldUs) {
                vTaskDelay(1);
                nextYieldUs = esp_timer_get_time() + 50000;
            }
        }
    }

    if (_ref->alignment_algo == 2) {
        CalculateSimularities(rgb_template, _ref->found_x, _ref->found_y,
                              min, avg, max, SAD, _ref->fastalg_SAD,
                              _ref->fastalg_SAD_criteria);
    }
    _ref->fastalg_x = _ref->found_x;
    _ref->fastalg_y = _ref->found_y;
    _ref->fastalg_min = min;
    _ref->fastalg_avg = avg;
    _ref->fastalg_max = max;
    _ref->fastalg_SAD = SAD;

    RGBImageRelease();
    stbi_image_free(rgb_template);
    return false;
}


// Compares the template with the image at (_startx, _starty) and reports whether the match is still as
// good as the stored one. Used by the "Fast" alignment to skip the full search.
bool CFindTemplate::CalculateSimularities(uint8_t* _rgb_tmpl, int _startx, int _starty, int &min, float &avg, int &max, float &SAD, float _SADold, float _SADcrit)
{
    int dif;
    int minDif = 255;
    int maxDif = -255;
    double avgDifSum = 0;
    long int anz = 0;
    double aktSAD = 0;    

    int xouter, youter, _ch;

    // The template has to lie completely inside the image at this position.
    if (_rgb_tmpl == NULL || rgb_image == NULL || width <= 0 || height <= 0 ||
        channels <= 0 || tpl_width <= 0 || tpl_height <= 0 ||
        _startx < 0 || _starty < 0 || tpl_width > width || tpl_height > height ||
        _startx > width - tpl_width || _starty > height - tpl_height) return false;

    // Walk the template, not the search area: the former loop ran over the search area size (plus one)
    // and so read past the end of the template buffer and compared pixels that are not part of the marker.
    for (xouter = 0; xouter < tpl_width; xouter++)
        for (youter = 0; youter < tpl_height; ++youter)
        {
            stbi_uc* p_org = rgb_image + (channels * ((youter + _starty) * width + (xouter + _startx)));
            stbi_uc* p_tpl = _rgb_tmpl + (channels * (youter * tpl_width + xouter));
            for (_ch = 0; _ch < channels; ++_ch)
            {
                dif = (int)p_tpl[_ch] - (int)p_org[_ch];
                aktSAD += dif * dif;                
                if (dif < minDif) minDif = dif;
                if (dif > maxDif) maxDif = dif;
                avgDifSum += dif;
                anz++;
            }
        }

    if (anz == 0) return false;
    avg = avgDifSum / anz;
    min = minDif;
    max = maxDif;
    SAD = sqrt(aktSAD) / anz;

    float _SADdif = abs(SAD - _SADold);

    ESP_LOGD(TAG, "Anzahl %ld, avgDifSum %fd, avg %f, SAD_neu: %fd, _SAD_old: %f, _SAD_crit:%f", anz, avgDifSum, avg, SAD, _SADold, _SADdif);

    if (_SADdif <= _SADcrit)
        return true;

    return false;
}



