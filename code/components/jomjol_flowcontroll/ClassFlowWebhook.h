#ifdef ENABLE_WEBHOOK

#pragma once

#ifndef CLASSFWEBHOOK_H
#define CLASSFWEBHOOK_H

#include <string>

#include "ClassFlow.h"

#include "ClassFlowPostProcessing.h"
#include "ClassFlowAlignment.h"

#include "Helper.h"

class ClassFlowWebhook : public ClassFlow
{
protected:
    std::string uri, apikey;

	ClassFlowPostProcessing* flowpostprocessing;
    ClassFlowAlignment* flowAlignment;

    bool WebhookEnable;
    int WebhookUploadImg;

    void SetInitialParameter(void); 

public:
    ClassFlowWebhook();
    ClassFlowWebhook(std::vector<ClassFlow*>* lfc);
    ClassFlowWebhook(std::vector<ClassFlow*>* lfc, ClassFlow *_prev);

    bool ReadParameter(FILE* pfile, std::string& aktparamgraph);
    bool doFlow(std::string time);
    std::string name() { return "ClassFlowWebhook"; };
};

#endif //CLASSFWEBHOOK_H
#endif //ENABLE_WEBHOOK
