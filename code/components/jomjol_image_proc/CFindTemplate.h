#pragma once

#ifndef CFINDTEMPLATE_H
#define CFINDTEMPLATE_H

#include "CImageBasis.h"

struct RefInfo {
    std::string image_file;
    int target_x = 0;       // X-coordinate of the alignment image
    int target_y = 0;       // Y-coordinate of the alignment image
    int width = 0;          // Width of the alignment image
    int height = 0;         // Height of the alignment image
    int search_x;           // X-size (width) in which the reference is searched
    int search_y;           // Y-size (height) in which the reference is searched
    int found_x;
    int found_y;
    int fastalg_x = -1;
    int fastalg_y = -1;
    int fastalg_min = -256;
    float fastalg_avg = -1;
    int fastalg_max = -1;
    float fastalg_SAD = -1;
    float fastalg_SAD_criteria = -1;
    int alignment_algo = 0; // 0 = "Default" (nur R-Kanal), 1 = "HighAccuracy" (RGB-Kanal), 2 = "Fast" (1.x RGB, dann isSimilar)
};

#define Alignment_OK 0
#define Alignment_Failed -1

class CFindTemplate : public CImageBasis
{
  public:
    int tpl_width, tpl_height, tpl_bpp;
    CFindTemplate(std::string name, uint8_t *_rgb_image, int _channels, int _width, int _height, int _bpp) : CImageBasis(name, _rgb_image, _channels, _width, _height, _bpp) {};
    bool FindTemplate(RefInfo *_ref);
    bool CalculateSimularities(uint8_t *_rgb_tmpl, int _startx, int _starty, int _sizex, int _sizey, int &min, float &avg, int &max, float &SAD, float _SADold, float _SADcrit);
};

#endif // CFINDTEMPLATE_H
