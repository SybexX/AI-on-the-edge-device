#pragma once

#ifndef CROTATEIMAGE_H
#define CROTATEIMAGE_H

#include "CImageBasis.h"

class CRotateImage : public CImageBasis
{
public:
    CImageBasis *ImageTMP, *ImageOrg;
    int pixel_fill_color = 216; // Gray

    CRotateImage(std::string name, std::string _image) : CImageBasis(name, _image)
    {
        ImageTMP = NULL;
        ImageOrg = NULL;
    };

    CRotateImage(std::string name, uint8_t *_rgb_image, int _channels, int _width, int _height, int _bpp) : CImageBasis(name, _rgb_image, _channels, _width, _height, _bpp)
    {
        ImageTMP = NULL;
        ImageOrg = NULL;
    };

    CRotateImage(std::string name, CImageBasis *_org, CImageBasis *_temp);

    void RotateImage(float _angle);
    void RotateImage(float _angle, int _centerx, int _centery);

    void RotateAntiAliasingImage(float _angle);
    void RotateAntiAliasingImage(float _angle, int _centerx, int _centery);

    void TranslateImage(int _dx, int _dy);
};

#endif // CROTATEIMAGE_H
