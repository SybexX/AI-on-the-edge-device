#pragma once

#ifndef CLASSFFLOWTAKEIMAGE_H
#define CLASSFFLOWTAKEIMAGE_H

#include "ClassFlowImage.h"
#include "ClassControllCamera.h"
#include "../../include/defines.h"

#include <string>

class ClassFlowTakeImage : public ClassFlowImage
{
protected:
    time_t TimeImageTaken;
    string namerawimage;

    bool takePictureWithFlash(int flash_duration);

    void SetInitialParameter(void);

public:
    CImageBasis *rawImage;

    ClassFlowTakeImage(std::vector<ClassFlow *> *lfc);

    bool ReadParameter(FILE *pfile, string &aktparamgraph);
    bool doFlow(string time);
    string getHTMLSingleStep(string host);
    time_t getTimeImageTaken(void);
    string name() { return "ClassFlowTakeImage"; };

    esp_err_t SendRawJPG(httpd_req_t *req, bool previewCapture);

    ~ClassFlowTakeImage(void);
};

#endif // CLASSFFLOWTAKEIMAGE_H