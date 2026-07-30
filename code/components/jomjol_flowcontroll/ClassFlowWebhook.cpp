#ifdef ENABLE_WEBHOOK
#include <time.h>
#include <sstream>
#include "esp_log.h"

#include "../../include/defines.h"
#include "Helper.h"

#include "ClassFlowWebhook.h"
#include "ClassFlowPostProcessing.h"
#include "ClassFlowAlignment.h"
#include "ClassLogFile.h"

#include "interface_webhook.h"
#include "connect_wlan.h"
#include "time_sntp.h"

static const char* TAG = "WEBHOOK";

ClassFlowWebhook::ClassFlowWebhook()
{
    SetInitialParameter();
}

void ClassFlowWebhook::SetInitialParameter(void)
{
    uri = "";
    apikey = "";
    flowpostprocessing = NULL;
    flowAlignment = NULL;
    previousElement = NULL;
    ListFlowControll = NULL;
    WebhookEnable = false;
    WebhookUploadImg = 0;
    disabled = false;
}       

ClassFlowWebhook::ClassFlowWebhook(std::vector<ClassFlow*>* lfc)
{
    SetInitialParameter();

    ListFlowControll = lfc;
    
    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing*) (*ListFlowControll)[i];
        }
        else if (((*ListFlowControll)[i])->name().compare("ClassFlowAlignment") == 0)
        {
            flowAlignment = (ClassFlowAlignment*) (*ListFlowControll)[i];
        }
    }
}

ClassFlowWebhook::ClassFlowWebhook(std::vector<ClassFlow*>* lfc, ClassFlow *_prev)
{
    SetInitialParameter();

    previousElement = _prev;
    ListFlowControll = lfc;

    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing*) (*ListFlowControll)[i];
        }
        else if (((*ListFlowControll)[i])->name().compare("ClassFlowAlignment") == 0)
        {
            flowAlignment = (ClassFlowAlignment*) (*ListFlowControll)[i];
        }
    }
}

bool ClassFlowWebhook::ReadParameter(FILE* pfile, std::string& aktparamgraph)
{
    aktparamgraph = trim(aktparamgraph);
    if (aktparamgraph.size() == 0)
    {
        if (!GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if (toUpper(aktparamgraph).compare("[WEBHOOK]") != 0)
    {
        return false;
    }

    std::vector<string> splitted;

    while (getNextLine(pfile, &aktparamgraph) && !isNewParagraph(aktparamgraph))
    {
        splitted = ZerlegeZeile(aktparamgraph);
        std::string _param = toUpper(GetParameterName(splitted[0]));

        if (splitted.size() > 1)
        {
            if (_param == "URI")
            {
                uri = splitted[1];
            }
            else if (_param == "APIKEY")
            {
                apikey = splitted[1];
            }
            else if (_param == "UPLOADIMG")
            {
                if (toUpper(splitted[1]) == "1")
                {
                    WebhookUploadImg = 1;
                }
                else if (toUpper(splitted[1]) == "2")
                {
                    WebhookUploadImg = 2;
                }
            }
        }
    }

    if (uri.empty())
    {
        WebhookEnable = false;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Webhook not enabled: no Uri configured");
    }
    else
    {
        WebhookEnable = true;
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Webhook Enabled for Uri " + uri);
    }

    WebhookInit(uri,apikey);

    return true;
}

bool ClassFlowWebhook::doFlow(std::string zwtime)
{
    if (!WebhookEnable)
    {
        return true;
    }

    if (flowpostprocessing)
    {
        ESP_LOGD(TAG, "before sending WebHook");
        bool numbersWithError = WebhookPublish(flowpostprocessing->GetNumbers());

        #ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
            if ((WebhookUploadImg == 1 || (WebhookUploadImg != 0 && numbersWithError)) && flowAlignment && flowAlignment->AlgROI) 
            {
                WebhookUploadPic(flowAlignment->AlgROI);
            }
        #endif
    }
       
    return true;
}
#endif //ENABLE_WEBHOOK
