#include "CFindTemplate.h"

#include "ClassLogFile.h"
#include "Helper.h"
#include "../../include/defines.h"

#include <esp_log.h>

static const char *TAG = "C FIND TEMPL";

// #define DEBUG_DETAIL_ON

bool CFindTemplate::FindTemplate(RefInfo *_ref)
{
    uint8_t *rgb_template;

    if (file_size(_ref->image_file.c_str()) == 0)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, _ref->image_file + " is empty!");
        return false;
    }

    rgb_template = stbi_load(_ref->image_file.c_str(), &tpl_width, &tpl_height, &tpl_bpp, channels);
    if (rgb_template == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to load " + _ref->image_file + "! Is it corrupted?");
        return false;
    }

    int ow_start, ow_stop;
    int oh_start, oh_stop;

    if (_ref->search_x == 0)
    {
        _ref->search_x = width;
        _ref->found_x = 0;
    }

    if (_ref->search_y == 0)
    {
        _ref->search_y = height;
        _ref->found_y = 0;
    }

    ow_start = _ref->target_x - _ref->search_x;
    ow_start = std::max(ow_start, 0);

    ow_stop = _ref->target_x + _ref->search_x;

    if ((ow_stop + tpl_width) > width)
	{
        ow_stop = width - tpl_width;
	}

    oh_start = _ref->target_y - _ref->search_y;
    oh_start = std::max(oh_start, 0);

    oh_stop = _ref->target_y + _ref->search_y;

    if ((oh_stop + tpl_height) > height)
	{
        oh_stop = height - tpl_height;
	}

    float avg = 0.0f;
    float SAD = 0.0f;
    int min = 0;
    int max = 0;

    bool isSimilar = false;

    if ((_ref->alignment_algo == 2) && (_ref->fastalg_x > -1) && (_ref->fastalg_y > -1))
    {
        const int ow = ow_stop - ow_start + 1;
        const int oh = oh_stop - oh_start + 1;

        isSimilar = CalculateSimularities(rgb_template, _ref->fastalg_x, _ref->fastalg_y, ow, oh, min, avg, max, SAD, _ref->fastalg_SAD, _ref->fastalg_SAD_criteria);
    }

    if (isSimilar)
    {
#ifdef DEBUG_DETAIL_ON
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Use FastAlignment sucessfull");
#endif

        _ref->found_x = _ref->fastalg_x;
        _ref->found_y = _ref->fastalg_y;

        stbi_image_free(rgb_template);

        return true;
    }

    const uint64_t maxPixelDifference = (uint64_t)tpl_width * (uint64_t)tpl_height * 255ULL;
    const double maxSAD = (double)maxPixelDifference * (double)maxPixelDifference;
    double minSAD = maxSAD;

    RGBImageLock();

    int anzchannels = channels;

    if (_ref->alignment_algo == 0)
	{
        anzchannels = 1;
	}

    const int imageRowBytes = width * channels;
    const int templateRowBytes = tpl_width * channels;

    if (anzchannels == 1)
    {
        for (int xouter = ow_start; xouter <= ow_stop; ++xouter)
        {
            for (int youter = oh_start; youter <= oh_stop; ++youter)
            {
                uint64_t aktSAD = 0;
                const uint8_t *p_tpl = rgb_template;
                const uint8_t *p_org = rgb_image + (youter * imageRowBytes) +(xouter * channels);

                for (int tpl_y = 0; tpl_y < tpl_height; ++tpl_y)
                {
                    const uint8_t *org = p_org;
                    const uint8_t *tpl = p_tpl;

                    for (int tpl_x = 0; tpl_x < tpl_width; ++tpl_x)
                    {
                        const int diff = (int)tpl[0] - (int)org[0];
                        aktSAD += (uint32_t)(diff * diff);
                        tpl += channels;
                        org += channels;
                    }

                    p_tpl += templateRowBytes;
                    p_org += imageRowBytes;
                }

                if (aktSAD < minSAD)
                {
                    minSAD = (double)aktSAD;
                    _ref->found_x = xouter;
                    _ref->found_y = youter;
                }
            }
        }
    }
    else if (anzchannels == 3)
    {
        for (int xouter = ow_start; xouter <= ow_stop; ++xouter)
        {
            for (int youter = oh_start; youter <= oh_stop; ++youter)
            {
                uint64_t aktSAD = 0;
                const uint8_t *p_tpl = rgb_template;
                const uint8_t *p_org = rgb_image + (youter * imageRowBytes) + (xouter * channels);

                for (int tpl_y = 0; tpl_y < tpl_height; ++tpl_y)
                {
                    const uint8_t *org = p_org;
                    const uint8_t *tpl = p_tpl;

                    for (int tpl_x = 0; tpl_x < tpl_width; ++tpl_x)
                    {
                        int diff = (int)tpl[0] - (int)org[0];
                        aktSAD += (uint32_t)(diff * diff);

                        diff = (int)tpl[1] - (int)org[1];
                        aktSAD += (uint32_t)(diff * diff);

                        diff = (int)tpl[2] - (int)org[2];
                        aktSAD += (uint32_t)(diff * diff);

                        tpl += 3;
                        org += channels;
                    }

                    p_tpl += templateRowBytes;
                    p_org += imageRowBytes;
                }

                if (aktSAD < minSAD)
                {
                    minSAD = (double)aktSAD;
                    _ref->found_x = xouter;
                    _ref->found_y = youter;
                }
            }
        }
    }
    else if (anzchannels == 4)
    {
        for (int xouter = ow_start; xouter <= ow_stop; ++xouter)
        {
            for (int youter = oh_start; youter <= oh_stop; ++youter)
            {
                uint64_t aktSAD = 0;
                const uint8_t *p_tpl = rgb_template;
                const uint8_t *p_org = rgb_image + (youter * imageRowBytes) + (xouter * channels);

                for (int tpl_y = 0; tpl_y < tpl_height; ++tpl_y)
                {
                    const uint8_t *org = p_org;
                    const uint8_t *tpl = p_tpl;

                    for (int tpl_x = 0; tpl_x < tpl_width; ++tpl_x)
                    {
                        int diff = (int)tpl[0] - (int)org[0];
                        aktSAD += (uint32_t)(diff * diff);

                        diff = (int)tpl[1] - (int)org[1];
                        aktSAD += (uint32_t)(diff * diff);

                        diff = (int)tpl[2] - (int)org[2];
                        aktSAD += (uint32_t)(diff * diff);

                        diff = (int)tpl[3] - (int)org[3];
                        aktSAD += (uint32_t)(diff * diff);

                        tpl += 4;
                        org += channels;
                    }

                    p_tpl += templateRowBytes;
                    p_org += imageRowBytes;
                }

                if (aktSAD < minSAD)
                {
                    minSAD = (double)aktSAD;
                    _ref->found_x = xouter;
                    _ref->found_y = youter;
                }
            }
        }
    }
    else
    {
        for (int xouter = ow_start; xouter <= ow_stop; ++xouter)
        {
            for (int youter = oh_start; youter <= oh_stop; ++youter)
            {
                uint64_t aktSAD = 0;
                const uint8_t *p_tpl = rgb_template;
                const uint8_t *p_org = rgb_image + (youter * imageRowBytes) + (xouter * channels);

                for (int tpl_y = 0; tpl_y < tpl_height; ++tpl_y)
                {
                    const uint8_t *org = p_org;
                    const uint8_t *tpl = p_tpl;

                    for (int tpl_x = 0; tpl_x < tpl_width; ++tpl_x)
                    {
                        for (int ch = 0; ch < anzchannels; ++ch)
                        {
                            const int diff = (int)tpl[ch] - (int)org[ch];
                            aktSAD += (uint32_t)(diff * diff);
                        }

                        tpl += channels;
                        org += channels;
                    }

                    p_tpl += templateRowBytes;
                    p_org += imageRowBytes;
                }

                if (aktSAD < minSAD)
                {
                    minSAD = (double)aktSAD;
                    _ref->found_x = xouter;
                    _ref->found_y = youter;
                }
            }
        }
    }

    if (_ref->alignment_algo == 2)
    {
        CalculateSimularities(rgb_template, _ref->found_x, _ref->found_y, ow_stop - ow_start + 1, oh_stop - oh_start + 1, min, avg, max, SAD, _ref->fastalg_SAD, _ref->fastalg_SAD_criteria);
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

bool CFindTemplate::CalculateSimularities(uint8_t *_rgb_tmpl, int _startx, int _starty, int _sizex, int _sizey, int &min, float &avg, int &max, float &SAD, float _SADold, float _SADcrit)
{
    int minDif = 255;
    int maxDif = -255;

    int64_t avgDifSum = 0;
    uint64_t anz = 0;
    uint64_t aktSAD = 0;

    const int imageRowBytes = width * channels;
    const int templateRowBytes = tpl_width * channels;

    if (channels == 3)
    {
        for (int xouter = 0; xouter <= _sizex; ++xouter)
        {
            for (int youter = 0; youter <= _sizey; ++youter)
            {
                const uint8_t *p_org = rgb_image + ((_starty + youter) * imageRowBytes) + ((_startx + xouter) * channels);
                const uint8_t *p_tpl = _rgb_tmpl + (youter * templateRowBytes) + (xouter * channels);

                for (int ch = 0; ch < 3; ++ch)
                {
                    const int dif = (int)p_tpl[ch] - (int)p_org[ch];
                    const int squared = dif * dif;

                    if (dif < minDif)
					{
                        minDif = dif;
					}

                    if (dif > maxDif)
					{
                        maxDif = dif;
					}

                    avgDifSum += dif;
                    aktSAD += (uint32_t)squared;
                    ++anz;
                }
            }
        }
    }
    else if (channels == 4)
    {
        for (int xouter = 0; xouter <= _sizex; ++xouter)
        {
            for (int youter = 0; youter <= _sizey; ++youter)
            {
                const uint8_t *p_org = rgb_image + ((_starty + youter) * imageRowBytes) + ((_startx + xouter) * channels);
                const uint8_t *p_tpl = _rgb_tmpl + (youter * templateRowBytes) + (xouter * channels);

                for (int ch = 0; ch < 4; ++ch)
                {
                    const int dif = (int)p_tpl[ch] - (int)p_org[ch];
                    const int squared = dif * dif;

                    if (dif < minDif)
					{
                        minDif = dif;
					}

                    if (dif > maxDif)
					{
                        maxDif = dif;
					}

                    avgDifSum += dif;
                    aktSAD += (uint32_t)squared;
                    ++anz;
                }
            }
        }
    }
    else
    {
        for (int xouter = 0; xouter <= _sizex; ++xouter)
        {
            for (int youter = 0; youter <= _sizey; ++youter)
            {
                const uint8_t *p_org = rgb_image + ((_starty + youter) * imageRowBytes) + ((_startx + xouter) * channels);
                const uint8_t *p_tpl = _rgb_tmpl + (youter * templateRowBytes) + (xouter * channels);

                for (int ch = 0; ch < channels; ++ch)
                {
                    const int dif = (int)p_tpl[ch] - (int)p_org[ch];
                    const int squared = dif * dif;

                    if (dif < minDif)
					{
                        minDif = dif;
					}

                    if (dif > maxDif)
					{
                        maxDif = dif;
					}

                    avgDifSum += dif;
                    aktSAD += (uint32_t)squared;
                    ++anz;
                }
            }
        }
    }

    avg = (float)avgDifSum / (float)anz;

    min = minDif;
    max = maxDif;

    SAD = sqrtf((float)aktSAD) / (float)anz;

    const float SADdif = fabsf(SAD - _SADold);

    ESP_LOGD(TAG, "Anzahl %lu, avgDifSum %lld, avg %f, SAD_neu: %f, _SAD_old: %f, _SAD_crit:%f", (unsigned long)anz, (long long)avgDifSum, avg, SAD, _SADold, SADdif);

    return (SADdif <= _SADcrit);
}
