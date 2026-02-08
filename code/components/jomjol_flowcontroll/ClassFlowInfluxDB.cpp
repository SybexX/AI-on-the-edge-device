#ifdef ENABLE_INFLUXDB
#include <sstream>
#include "ClassFlowInfluxDB.h"
#include "Helper.h"
#include "connect_wifi.h"

#include "time_sntp.h"
#include "interface_influxdb.h"

#include "ClassFlowPostProcessing.h"
#include "esp_log.h"
#include "defines.h"

#include "ClassLogFile.h"

#include <time.h>

static const char *TAG = "INFLUXDB";

void ClassFlowInfluxDB::SetInitialParameter(void)
{
    uri = "";
    database = "";

    OldValue = "";
    flowpostprocessing = NULL;
    user = "";
    password = "";
    previousElement = NULL;
    ListFlowControll = NULL;
    disabled = false;
    InfluxDBenable = false;
}

ClassFlowInfluxDB::ClassFlowInfluxDB()
{
    SetInitialParameter();
}

ClassFlowInfluxDB::ClassFlowInfluxDB(std::vector<ClassFlow *> *lfc)
{
    SetInitialParameter();

    ListFlowControll = lfc;
    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing *)(*ListFlowControll)[i];
        }
    }
}

ClassFlowInfluxDB::ClassFlowInfluxDB(std::vector<ClassFlow *> *lfc, ClassFlow *_prev)
{
    SetInitialParameter();

    previousElement = _prev;
    ListFlowControll = lfc;

    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing *)(*ListFlowControll)[i];
        }
    }
}

bool ClassFlowInfluxDB::ReadParameter(FILE *pfile, string &aktparamgraph)
{
    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0)
    {
        if (!this->GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if (toUpper(aktparamgraph).compare("[INFLUXDB]") != 0)
    {
        return false;
    }

    std::vector<string> splitted;

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        splitted = split_line(aktparamgraph);

        if (splitted.size() > 1)
        {
            std::string _param = GetParameterName(splitted[0]);

            if (toUpper(_param) == "USER")
            {
                this->user = splitted[1];
            }
            else if (toUpper(_param) == "PASSWORD")
            {
                this->password = splitted[1];
            }
            else if (toUpper(_param) == "URI")
            {
                this->uri = splitted[1];
            }
            else if (toUpper(_param) == "DATABASE")
            {
                this->database = splitted[1];
            }
            else if (toUpper(_param) == "MEASUREMENT")
            {
                handleMeasurement(splitted[0], splitted[1]);
            }
            else if (toUpper(_param) == "FIELD")
            {
                handleFieldname(splitted[0], splitted[1]);
            }
        }
    }

    if ((uri.length() > 0) && (database.length() > 0))
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Init InfluxDB with uri: " + uri + ", user: " + user + ", password: " + password);
        influxDB.InfluxDBInitV1(uri, database, user, password);
        InfluxDBenable = true;
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "InfluxDB init skipped as we are missing some parameters");
    }

    return true;
}

bool ClassFlowInfluxDB::doFlow(string zwtime)
{
    if (!InfluxDBenable)
    {
        return true;
    }

    std::string result;
    std::string measurement;
    std::string resulterror = "";
    std::string resultraw = "";
    std::string resultrate = "";
    std::string resulttimestamp = "";
    long int timeutc;
    string zw = "";
    string namenumber = "";

    if (flowpostprocessing)
    {
        std::vector<NumberPost *> *NUMBERS = flowpostprocessing->GetNumbers();

        for (int i = 0; i < (*NUMBERS).size(); ++i)
        {
            measurement = (*NUMBERS)[i]->MeasurementV1;
            result = (*NUMBERS)[i]->ReturnValue;
            resultraw = (*NUMBERS)[i]->ReturnRawValue;
            resulterror = (*NUMBERS)[i]->ErrorMessageText;
            resultrate = (*NUMBERS)[i]->ReturnRateValue;
            resulttimestamp = (*NUMBERS)[i]->timeStamp;
            timeutc = (*NUMBERS)[i]->timeStampTimeUTC;

            if ((*NUMBERS)[i]->FieldV1.length() > 0)
            {
                namenumber = (*NUMBERS)[i]->FieldV1;
            }
            else
            {
                namenumber = (*NUMBERS)[i]->name;
                if (namenumber == "default")
                {
                    namenumber = "value";
                }
                else
                {
                    namenumber = namenumber + "/value";
                }
            }

            if (result.length() > 0)
            {
                influxDB.InfluxDBPublish(measurement, namenumber, result, timeutc);
            }
        }
    }

    OldValue = result;

    return true;
}

void ClassFlowInfluxDB::handleMeasurement(string _decsep, string _value)
{
    string _digit = "default";
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1)
    {
        _digit = _decsep.substr(0, _pospunkt);
    }

    for (int j = 0; j < flowpostprocessing->NUMBERS.size(); ++j)
    {
        if ((flowpostprocessing->NUMBERS[j]->name == _digit) || (_digit == "default"))
        {
            flowpostprocessing->NUMBERS[j]->MeasurementV1 = _value;
        }
    }
}

void ClassFlowInfluxDB::handleFieldname(string _decsep, string _value)
{
    string _digit = "default";
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1)
    {
        _digit = _decsep.substr(0, _pospunkt);
    }

    for (int j = 0; j < flowpostprocessing->NUMBERS.size(); ++j)
    {
        if ((flowpostprocessing->NUMBERS[j]->name == _digit) || (_digit == "default"))
        {
            flowpostprocessing->NUMBERS[j]->FieldV1 = _value;
        }
    }
}

#endif // ENABLE_INFLUXDB
