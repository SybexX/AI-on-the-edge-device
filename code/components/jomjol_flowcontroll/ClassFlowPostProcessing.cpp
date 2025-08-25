#include <iomanip>
#include <sstream>
#include <time.h>
#include <esp_log.h>
#include <driver/uart.h>

#include "ClassFlowPostProcessing.h"
#include "ClassFlowTakeImage.h"
#include "MainFlowControl.h"
#include "ClassLogFile.h"
#include "time_sntp.h"
#include "Helper.h"

#include "defines.h"

static const char *TAG = "POSTPROC";

std::string ClassFlowPostProcessing::getNumbersName(void)
{
    std::string ret = "";

    for (int i = 0; i < NUMBERS.size(); ++i) {
        ret += NUMBERS[i]->name;

        if (i < NUMBERS.size() - 1) {
            ret = ret + "\t";
        }
    }

    // ESP_LOGI(TAG, "Result ClassFlowPostProcessing::getNumbersName: %s", ret.c_str());
    return ret;
}

std::string ClassFlowPostProcessing::GetJSON(std::string _lineend)
{
    std::string json = "{" + _lineend;

    for (int i = 0; i < NUMBERS.size(); ++i) {
        json += "\"" + NUMBERS[i]->name + "\":" + _lineend;
        json += getJsonFromNumber(i, _lineend) + _lineend;

        if ((i + 1) < NUMBERS.size()) {
            json += "," + _lineend;
        }
    }

    json += "}";

    return json;
}

std::string ClassFlowPostProcessing::getJsonFromNumber(int i, std::string _lineend)
{
    std::string json = "";

    json += "  {" + _lineend;

    if (NUMBERS[i]->ReturnValue.length() > 0) {
        json += "    \"value\": \"" + NUMBERS[i]->ReturnValue + "\"," + _lineend;
    }
    else {
        json += "    \"value\": \"\"," + _lineend;
    }

    json += "    \"raw\": \"" + NUMBERS[i]->ReturnRawValue + "\"," + _lineend;
    json += "    \"pre\": \"" + NUMBERS[i]->ReturnPreValue + "\"," + _lineend;
    json += "    \"error\": \"" + NUMBERS[i]->ErrorMessageText + "\"," + _lineend;

    if (NUMBERS[i]->ReturnRateValue.length() > 0) {
        json += "    \"rate\": \"" + NUMBERS[i]->ReturnRateValue + "\"," + _lineend;
    }
    else {
        json += "    \"rate\": \"\"," + _lineend;
    }

    if (NUMBERS[i]->ReturnChangeAbsolute.length() > 0) {
        json += "    \"absrate\": \"" + NUMBERS[i]->ReturnChangeAbsolute + "\"," + _lineend;
    }
    else {
        json += "    \"absrate\": \"\"," + _lineend;
    }

    json += "    \"timestamp\": \"" + NUMBERS[i]->timeStamp + "\"" + _lineend;
    json += "  }" + _lineend;

    return json;
}

std::string ClassFlowPostProcessing::GetPreValue(std::string _number)
{
    std::string result;
    int index = -1;

    if (_number == "") {
        _number = "default";
    }

    for (int i = 0; i < NUMBERS.size(); ++i) {
        if (NUMBERS[i]->name == _number) {
            index = i;
        }
    }

    if (index == -1) {
        return std::string("");
    }

    result = RundeOutput(NUMBERS[index]->PreValue, NUMBERS[index]->Nachkomma);

    return result;
}

bool ClassFlowPostProcessing::SetPreValue(double _newvalue, std::string _numbers, bool _extern)
{
    // ESP_LOGD(TAG, "SetPrevalue: %f, %s", _newvalue, _numbers.c_str());

    for (int index = 0; index < NUMBERS.size(); ++index) {
        // ESP_LOGD(TAG, "Number %d, %s", index, NUMBERS[index]->name.c_str());

        if (NUMBERS[index]->name == _numbers) {
            if (_newvalue >= 0) {
                // if new value posivive, use provided value to preset PreValue
                NUMBERS[index]->PreValue = _newvalue;
            }
            else {
                // if new value negative, use last raw value to preset PreValue
                char *p;
                double ReturnRawValueAsDouble = strtod(NUMBERS[index]->ReturnRawValue.c_str(), &p);

                if (ReturnRawValueAsDouble == 0) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SetPreValue: RawValue not a valid value for further processing: " + NUMBERS[index]->ReturnRawValue);
                    return false;
                }

                NUMBERS[index]->PreValue = ReturnRawValueAsDouble;
            }

            // NUMBERS[index]->ReturnPreValue = std::to_string(NUMBERS[index]->PreValue);
            NUMBERS[index]->ReturnPreValue = RundeOutput(NUMBERS[index]->PreValue, NUMBERS[index]->Nachkomma);
            NUMBERS[index]->PreValueOkay = true;

            if (_extern) {
                time(&(NUMBERS[index]->timeStampLastPreValue));
                localtime(&(NUMBERS[index]->timeStampLastPreValue));
            }

            // ESP_LOGD(TAG, "Found %d! - set to %.8f", index,  NUMBERS[index]->PreValue);

            UpdatePreValueINI = true; // Only update prevalue file if a new value is set
            SavePreValue();

            // LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SetPreValue: PreValue for " + NUMBERS[index]->name + " set to " + std::to_string(NUMBERS[index]->PreValue));
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SetPreValue: PreValue for " + NUMBERS[index]->name + " set to " + RundeOutput(NUMBERS[index]->PreValue, NUMBERS[index]->Nachkomma));
            return true;
        }
    }

    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SetPreValue: Numbersname not found or not valid");
    return false; // No new value was set (e.g. wrong numbersname, no numbers at all)
}

bool ClassFlowPostProcessing::LoadPreValue(void)
{
    UpdatePreValueINI = false; // Conversion to the new format

    FILE *pFile = fopen(FilePreValue.c_str(), "r");
    if (pFile == NULL) {
        ESP_LOGE(TAG, "/sdcard/config/prevalue.ini does not exist!");
        return false;
    }

    char buf[1024];

    if (!fgets(buf, 1024, pFile)) {
        fclose(pFile);
        ESP_LOGE(TAG, "/sdcard/config/prevalue.ini empty!");
        return false;
    }

    ESP_LOGD(TAG, "Read line Prevalue.ini: %s", buf);
    std::string line = trim_string_left_right(std::string(buf));

    if (line.length() == 0) {
        fclose(pFile);
        ESP_LOGE(TAG, "/sdcard/config/prevalue.ini empty!");
        return false;
    }

    time_t tStart;
    time(&tStart);
    int yy, month, dd, hh, mm, ss;
    struct tm whenStart;

    std::string _prevalue_time, _prevalue, _prevalue_name;

    std::vector<std::string> splitted;
    splitted = splitLine(line, "\t");

    //  Conversion to the new format
    if (splitted.size() > 1) {
        while ((splitted.size() > 1) && !(feof(pFile))) {
            _prevalue_name = trim_string_left_right(splitted[0]);
            _prevalue_time = trim_string_left_right(splitted[1]);
            _prevalue = trim_string_left_right(splitted[2]);

            for (int j = 0; j < NUMBERS.size(); ++j) {
                if (NUMBERS[j]->name == _prevalue_name) {
                    NUMBERS[j]->PreValue = std::stod(_prevalue.c_str());
                    NUMBERS[j]->ReturnPreValue = _prevalue;
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = std::to_string(NUMBERS[j]->Value);
                    NUMBERS[j]->ReturnRawValue = NUMBERS[j]->ReturnValue;

                    sscanf(_prevalue_time.c_str(), PREVALUE_TIME_FORMAT_INPUT, &yy, &month, &dd, &hh, &mm, &ss);
                    whenStart.tm_year = yy - 1900;
                    whenStart.tm_mon = month - 1;
                    whenStart.tm_mday = dd;
                    whenStart.tm_hour = hh;
                    whenStart.tm_min = mm;
                    whenStart.tm_sec = ss;
                    whenStart.tm_isdst = -1;
                    ESP_LOGD(TAG, "TIME: %d, %d, %d, %d, %d, %d", whenStart.tm_year, whenStart.tm_mon, whenStart.tm_wday, whenStart.tm_hour, whenStart.tm_min, whenStart.tm_sec);

                    NUMBERS[j]->timeStampLastPreValue = mktime(&whenStart);

                    if (!getTimeIsSet()) {
                        struct timeval set_time;
                        set_time.tv_sec = NUMBERS[j]->timeStampLastPreValue;
                        set_time.tv_usec = 0;
                        settimeofday(&set_time, 0);
                    }

                    localtime(&tStart);
                    double difference = (difftime(tStart, NUMBERS[j]->timeStampLastPreValue)) / 60;

                    if (difference > PreValueAgeStartup) {
                        NUMBERS[j]->PreValueOkay = false;
                    }
                    else {
                        NUMBERS[j]->PreValueOkay = true;
                    }
                }
            }

            if (fgets(buf, 1024, pFile) && !feof(pFile)) {
                ESP_LOGD(TAG, "Read line Prevalue.ini: %s", buf);
                splitted = splitLine(trim_string_left_right(std::string(buf)), "\t");

                if (splitted.size() > 1) {
                    _prevalue_name = trim_string_left_right(splitted[0]);
                    _prevalue_time = trim_string_left_right(splitted[1]);
                    _prevalue = trim_string_left_right(splitted[2]);
                }
            }
        }
        fclose(pFile);
    }
    else {
        // Old Format
        _prevalue_time = trim_string_left_right(splitted[0]);

        if (!fgets(buf, 1024, pFile)) {
            fclose(pFile);
            ESP_LOGE(TAG, "/sdcard/config/prevalue.ini value empty!");
            return false;
        }
        fclose(pFile);
        ESP_LOGD(TAG, "%s", buf);

        _prevalue = trim_string_left_right(std::string(buf));
        NUMBERS[0]->PreValue = std::stod(_prevalue.c_str());
        NUMBERS[0]->Value = NUMBERS[0]->PreValue;
        NUMBERS[0]->ReturnValue = std::to_string(NUMBERS[0]->Value);
        NUMBERS[0]->ReturnRawValue = NUMBERS[0]->ReturnValue;

        sscanf(_prevalue_time.c_str(), PREVALUE_TIME_FORMAT_INPUT, &yy, &month, &dd, &hh, &mm, &ss);
        whenStart.tm_year = yy - 1900;
        whenStart.tm_mon = month - 1;
        whenStart.tm_mday = dd;
        whenStart.tm_hour = hh;
        whenStart.tm_min = mm;
        whenStart.tm_sec = ss;
        whenStart.tm_isdst = -1;
        ESP_LOGD(TAG, "TIME: %d, %d, %d, %d, %d, %d", whenStart.tm_year, whenStart.tm_mon, whenStart.tm_wday, whenStart.tm_hour, whenStart.tm_min, whenStart.tm_sec);

        NUMBERS[0]->timeStampLastPreValue = mktime(&whenStart);

        if (!getTimeIsSet()) {
            struct timeval set_time;
            set_time.tv_sec = NUMBERS[0]->timeStampLastPreValue;
            set_time.tv_usec = 0;
            settimeofday(&set_time, 0);
        }

        localtime(&tStart);
        double difference = (difftime(tStart, NUMBERS[0]->timeStampLastPreValue)) / 60;

        if (difference > PreValueAgeStartup) {
            NUMBERS[0]->PreValueOkay = false;
        }
        else {
            NUMBERS[0]->PreValueOkay = true;
        }

        UpdatePreValueINI = true; // Conversion to the new format
        SavePreValue();
    }

    return true;
}

void ClassFlowPostProcessing::SavePreValue(void)
{
    // PreValues unchanged --> File does not have to be rewritten
    if (!UpdatePreValueINI) {
        return;
    }

    FILE *pFile = fopen(FilePreValue.c_str(), "w");

    for (int j = 0; j < NUMBERS.size(); ++j) {
        char buffer[80];
        struct tm *timeinfo = localtime(&NUMBERS[j]->timeStampLastPreValue);
        strftime(buffer, 80, PREVALUE_TIME_FORMAT_OUTPUT, timeinfo);
        NUMBERS[j]->timeStamp = std::string(buffer);
        NUMBERS[j]->timeStampTimeUTC = NUMBERS[j]->timeStampLastPreValue;
        // ESP_LOGD(TAG, "SaverPreValue %d, Value: %f, Nachkomma %d", j, NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);

        std::string temp_bufer = NUMBERS[j]->name + "\t" + NUMBERS[j]->timeStamp + "\t" + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) + "\n";
        ESP_LOGD(TAG, "Write PreValue line: %s", temp_bufer.c_str());

        if (pFile) {
            fputs(temp_bufer.c_str(), pFile);
        }
    }

    UpdatePreValueINI = false;

    fclose(pFile);
}

ClassFlowPostProcessing::ClassFlowPostProcessing(std::vector<ClassFlow *> *lfc, ClassFlowCNNGeneral *_analog, ClassFlowCNNGeneral *_digit)
{
    PreValueUse = false;
    PreValueAgeStartup = 30;
    ErrorMessage = false;
    FilePreValue = FormatFileName("/sdcard/config/prevalue.ini");
    ListFlowControll = lfc;
    flowTakeImage = NULL;
    UpdatePreValueINI = false;
    flowAnalog = _analog;
    flowDigit = _digit;

    for (int i = 0; i < ListFlowControll->size(); ++i) {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowTakeImage") == 0) {
            flowTakeImage = (ClassFlowTakeImage *)(*ListFlowControll)[i];
        }
    }
}

void ClassFlowPostProcessing::handleDecimalExtendedResolution(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool temp_value = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->isExtendedResolution = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleDecimalSeparator(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        int temp_value = 0;

        if (isStringNumeric(_value)) {
            temp_value = std::stoi(_value);
        }

        //  Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->DecimalShift = temp_value;
            NUMBERS[j]->DecimalShiftInitial = temp_value;
        }

        NUMBERS[j]->Nachkomma = NUMBERS[j]->AnzahlAnalog - NUMBERS[j]->DecimalShift;
    }
}

void ClassFlowPostProcessing::handleAnalogToDigitTransitionStart(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        float temp_value = 9.2;

        if (isStringNumeric(_value)) {
            temp_value = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->AnalogToDigitTransitionStart = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleAllowNegativeRate(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool temp_value = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->AllowNegativeRates = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleIgnoreLeadingNaN(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool temp_value = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->IgnoreLeadingNaN = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleMaxFlowRate(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        float temp_value = 4.0;

        if (isStringNumeric(_value)) {
            temp_value = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->useMaxFlowRate = true;
            NUMBERS[j]->MaxFlowRate = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleMaxRateType(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        t_RateType temp_value = AbsoluteChange;

        if (toUpper(_value) == "RATECHANGE") {
            temp_value = RateChange;
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->MaxRateType = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleMaxRateValue(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        float temp_value = 0.1;

        if (isStringNumeric(_value)) {
            temp_value = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->useMaxRateValue = true;
            NUMBERS[j]->MaxRateValue = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handleChangeRateThreshold(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        int temp_value = 2;

        if (isStringNumeric(_value)) {
            temp_value = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->ChangeRateThreshold = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handlecheckDigitIncreaseConsistency(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool temp_value = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->checkDigitIncreaseConsistency = temp_value;
        }
    }
}

void ClassFlowPostProcessing::handlecheckValueIncreaseConsistency(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool temp_value = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->checkValueIncreaseConsistency = temp_value;
        }
    }
}

bool ClassFlowPostProcessing::ReadParameter(FILE *pfile, std::string &aktparamgraph)
{
    std::vector<std::string> splitted;
    aktparamgraph = trim_string_left_right(aktparamgraph);

    if (aktparamgraph.size() == 0) {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }

    // Paragraph does not fit PostProcessing
    if (toUpper(aktparamgraph).compare("[POSTPROCESSING]") != 0) {
        return false;
    }

    InitNUMBERS();

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph)) {
        splitted = splitLine(aktparamgraph);
        std::string _param = toUpper(GetParameterName(splitted[0]));

        if (splitted.size() > 1) {
            if (_param == "EXTENDEDRESOLUTION") {
                handleDecimalExtendedResolution(splitted[0], splitted[1]);
            }
            else if (_param == "DECIMALSHIFT") {
                handleDecimalSeparator(splitted[0], splitted[1]);
            }
            else if (_param == "ANALOGTODIGITTRANSITIONSTART") {
                handleAnalogToDigitTransitionStart(splitted[0], splitted[1]);
            }
            else if (_param == "MAXFLOWRATE") {
                handleMaxFlowRate(splitted[0], splitted[1]);
            }
            else if (_param == "MAXRATEVALUE") {
                handleMaxRateValue(splitted[0], splitted[1]);
            }
            else if (_param == "MAXRATETYPE") {
                handleMaxRateType(splitted[0], splitted[1]);
            }
            else if (_param == "PREVALUEUSE") {
                PreValueUse = alphanumericToBoolean(splitted[1]);
            }
            else if (_param == "CHANGERATETHRESHOLD") {
                handleChangeRateThreshold(splitted[0], splitted[1]);
            }
            else if (_param == "CHECKDIGITINCREASECONSISTENCY") {
                handlecheckDigitIncreaseConsistency(splitted[0], splitted[1]);
            }
            else if (_param == "CHECKVALUEINCREASECONSISTENCY") {
                handlecheckValueIncreaseConsistency(splitted[0], splitted[1]);
            }
            else if (_param == "ALLOWNEGATIVERATES") {
                handleAllowNegativeRate(splitted[0], splitted[1]);
            }
            else if (_param == "ERRORMESSAGE") {
                ErrorMessage = alphanumericToBoolean(splitted[1]);
            }
            else if (_param == "IGNORELEADINGNAN") {
                handleIgnoreLeadingNaN(splitted[0], splitted[1]);
            }
            else if (_param == "PREVALUEAGESTARTUP") {
                if (isStringNumeric(splitted[1])) {
                    PreValueAgeStartup = std::stoi(splitted[1]);
                }
            }
        }
    }

    if (PreValueUse) {
        return LoadPreValue();
    }

    return true;
}

void ClassFlowPostProcessing::InitNUMBERS(void)
{
    std::vector<std::string> name_numbers;

    int anzDIGIT = 0;
    int anzANALOG = 0;

    if (flowDigit) {
        anzDIGIT = flowDigit->getNumberGENERAL();
        flowDigit->UpdateNameNumbers(&name_numbers);
    }

    if (flowAnalog) {
        anzANALOG = flowAnalog->getNumberGENERAL();
        flowAnalog->UpdateNameNumbers(&name_numbers);
    }

    ESP_LOGD(TAG, "Anzahl NUMBERS: %d - DIGITS: %d, ANALOG: %d", name_numbers.size(), anzDIGIT, anzANALOG);

    for (int _num = 0; _num < name_numbers.size(); ++_num) {
        NumberPost *_number = new NumberPost;

        _number->name = name_numbers[_num];

        _number->digit_roi = NULL;

        if (flowDigit) {
            _number->digit_roi = flowDigit->FindGENERAL(name_numbers[_num]);
        }

        if (_number->digit_roi) {
            _number->AnzahlDigit = _number->digit_roi->ROI.size();
        }
        else {
            _number->AnzahlDigit = 0;
        }

        _number->analog_roi = NULL;

        if (flowAnalog) {
            _number->analog_roi = flowAnalog->FindGENERAL(name_numbers[_num]);
        }

        if (_number->analog_roi) {
            _number->AnzahlAnalog = _number->analog_roi->ROI.size();
        }
        else {
            _number->AnzahlAnalog = 0;
        }

        _number->MaxFlowRate = 4.0;
        _number->useMaxFlowRate = false;
        _number->FlowRateAct = 0.0; // m3 / min
        _number->PreValueOkay = false;
        _number->AllowNegativeRates = false;
        _number->IgnoreLeadingNaN = false;
        _number->MaxRateType = AbsoluteChange;
        _number->MaxRateValue = 0.1;
        _number->useMaxRateValue = false;
        _number->checkDigitIncreaseConsistency = false;
        _number->checkValueIncreaseConsistency = false;
        _number->DecimalShift = 0;
        _number->DecimalShiftInitial = 0;
        _number->isExtendedResolution = false;
        _number->AnalogToDigitTransitionStart = 9.2;
        _number->ChangeRateThreshold = 2;

        _number->Value = 0.0;            // last value read out, incl. corrections
        _number->ReturnValue = "0.0";    // corrected return value, possibly with error message
        _number->ReturnRawValue = "0.0"; // raw value (with N & leading 0)
        _number->PreValue = 0.0;         // last value read out well
        _number->ReturnPreValue = "0.0";
        _number->ErrorMessageText = "no error"; // Error message for consistency check

        _number->Nachkomma = _number->AnzahlAnalog;

        NUMBERS.push_back(_number);
    }

    for (int i = 0; i < NUMBERS.size(); ++i) {
        ESP_LOGD(TAG, "Number %s, Anz DIG: %d, Anz ANA %d", NUMBERS[i]->name.c_str(), NUMBERS[i]->AnzahlDigit, NUMBERS[i]->AnzahlAnalog);
    }
}

std::string ClassFlowPostProcessing::ShiftDecimal(std::string in, int _decShift)
{
    if (_decShift == 0) {
        return in;
    }

    int _pos_dec_org, _pos_dec_neu;

    _pos_dec_org = findDelimiterPos(in, ".");

    if (_pos_dec_org == std::string::npos) {
        _pos_dec_org = in.length();
    }
    else {
        in = in.erase(_pos_dec_org, 1);
    }

    _pos_dec_neu = _pos_dec_org + _decShift;

    // comma is before the first digit
    if (_pos_dec_neu <= 0) {
        for (int i = 0; i > _pos_dec_neu; --i) {
            in = in.insert(0, "0");
        }

        in = "0." + in;
        return in;
    }

    // Comma should be after string (123 --> 1230)
    if (_pos_dec_neu > in.length()) {
        for (int i = in.length(); i < _pos_dec_neu; ++i) {
            in = in.insert(in.length(), "0");
        }
        return in;
    }

    std::string temp_bufer = (in.substr(0, _pos_dec_neu)) + "." + in.substr(_pos_dec_neu, in.length() - _pos_dec_neu);

    return temp_bufer;
}

bool ClassFlowPostProcessing::doFlow(std::string _time)
{
    time_t imagetime = flowTakeImage->getTimeImageTaken();

    if (imagetime == 0) {
        time(&imagetime);
    }

    struct tm *timeinfo = localtime(&imagetime);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S", timeinfo);
    _time = std::string(strftime_buf);

    ESP_LOGD(TAG, "Quantity NUMBERS: %d", NUMBERS.size());

    for (int j = 0; j < NUMBERS.size(); ++j) {
        NUMBERS[j]->Value = NUMBERS[j]->PreValue;
        NUMBERS[j]->ReturnValue = std::to_string(NUMBERS[j]->PreValue);
        NUMBERS[j]->ReturnRawValue = std::to_string(NUMBERS[j]->PreValue);
        NUMBERS[j]->ReturnRateValue = "0.0";
        NUMBERS[j]->ReturnChangeAbsolute = "0.0";
        NUMBERS[j]->ErrorMessageText = "";
        NUMBERS[j]->OverflowValue = 0.0;

        double _ratedifference = 0.0;

        // calculate time difference
        // double LastValueTimeDifference = ((difftime(imagetime, NUMBERS[j]->timeStampLastValue))  / 60); // in minutes
        double LastPreValueTimeDifference = ((difftime(imagetime, NUMBERS[j]->timeStampLastPreValue)) / 60); // in minutes

        // if ((!flowctrl.alignmentOk) && (!NUMBERS[j]->AlignmentFailsValue))
        if (!flowctrl.AlignmentOk) {
            NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "Alignment failed - Read: " + NUMBERS[j]->ReturnValue + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) +
                                           " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);
            NUMBERS[j]->Value = NUMBERS[j]->PreValue;
            NUMBERS[j]->ReturnValue = "";
            NUMBERS[j]->timeStampLastValue = imagetime;

            std::string temp_bufer = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, temp_bufer);
            WriteDataLog(j);
            continue;
        }

        // Update decimal point, as the decimal places can also change when changing from CNNType Auto --> xyz:
        UpdateNachkommaDecimalShift();

        int previous_value = -1;

        if (NUMBERS[j]->analog_roi) {
            NUMBERS[j]->ReturnRawValue = flowAnalog->getReadout(j, NUMBERS[j]->isExtendedResolution);

            if (NUMBERS[j]->ReturnRawValue.length() > 0) {
                char temp_bufer = NUMBERS[j]->ReturnRawValue[0];

                if (temp_bufer >= 48 && temp_bufer <= 57) {
                    previous_value = temp_bufer - 48;
                }
            }
        }
        ESP_LOGD(TAG, "After analog->getReadout: ReturnRawValue %s", NUMBERS[j]->ReturnRawValue.c_str());

        if (NUMBERS[j]->digit_roi && NUMBERS[j]->analog_roi) {
            NUMBERS[j]->ReturnRawValue = "." + NUMBERS[j]->ReturnRawValue;
        }

        if (NUMBERS[j]->digit_roi) {
            if (NUMBERS[j]->analog_roi) {
                NUMBERS[j]->ReturnRawValue = flowDigit->getReadout(j, false, previous_value, NUMBERS[j]->analog_roi->ROI[0]->result_float, NUMBERS[j]->AnalogToDigitTransitionStart) + NUMBERS[j]->ReturnRawValue;
            }
            else {
                NUMBERS[j]->ReturnRawValue = flowDigit->getReadout(j, NUMBERS[j]->isExtendedResolution, previous_value); // Extended Resolution only if there are no analogue digits
            }
        }
        ESP_LOGD(TAG, "After digit->getReadout: ReturnRawValue %s", NUMBERS[j]->ReturnRawValue.c_str());

        NUMBERS[j]->ReturnRawValue = ShiftDecimal(NUMBERS[j]->ReturnRawValue, NUMBERS[j]->DecimalShift);
        ESP_LOGD(TAG, "After ShiftDecimal: ReturnRawValue %s", NUMBERS[j]->ReturnRawValue.c_str());

        NUMBERS[j]->ReturnValue = NUMBERS[j]->ReturnRawValue;
        std::string OverflowValue = NUMBERS[j]->ReturnRawValue;
        for (int i = 0; i < OverflowValue.length(); ++i) {
            if (OverflowValue[i] != '.') {
                OverflowValue.replace(i, 1, "9");
            }
        }
        NUMBERS[j]->OverflowValue = std::stod(OverflowValue);

        if (NUMBERS[j]->IgnoreLeadingNaN) {
            while ((NUMBERS[j]->ReturnValue.length() > 1) && (NUMBERS[j]->ReturnValue[0] == 'N') && (NUMBERS[j]->ReturnValue[1] != '.')) {
                NUMBERS[j]->ReturnValue.erase(0, 1);
            }
        }
        ESP_LOGD(TAG, "After IgnoreLeadingNaN: ReturnValue %s", NUMBERS[j]->ReturnValue.c_str());

        if (findDelimiterPos(NUMBERS[j]->ReturnValue, "N") != std::string::npos) {
            if (PreValueUse && NUMBERS[j]->PreValueOkay) {
                NUMBERS[j]->ReturnValue = ErsetzteN(NUMBERS[j]->ReturnValue, NUMBERS[j]->PreValue);
            }
            else {
                NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "NaN available and PreValue too old - Read: " + NUMBERS[j]->ReturnValue + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) +
                                               " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);
                NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                NUMBERS[j]->ReturnValue = "";
                NUMBERS[j]->timeStampLastValue = imagetime;

                std::string temp_bufer = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, temp_bufer);
                WriteDataLog(j);
                continue; // there is no number because there is still an N.
            }
        }
        ESP_LOGD(TAG, "After findDelimiterPos: ReturnValue %s", NUMBERS[j]->ReturnValue.c_str());

        // Delete leading zeros (unless there is only one 0 left)
        while ((NUMBERS[j]->ReturnValue.length() > 1) && (NUMBERS[j]->ReturnValue[0] == '0') && (NUMBERS[j]->ReturnValue[1] != '.')) {
            NUMBERS[j]->ReturnValue.erase(0, 1);
        }
        ESP_LOGD(TAG, "After removeLeadingZeros: ReturnValue %s", NUMBERS[j]->ReturnValue.c_str());

        NUMBERS[j]->Value = std::stod(NUMBERS[j]->ReturnValue);

        if (NUMBERS[j]->checkDigitIncreaseConsistency) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "checkDigitIncreaseConsistency = true - This function can cause several problems, so please deactivate it if RawValue and Value differ greatly and the round is still rated as OK!");
            if (flowDigit) {
                NUMBERS[j]->Value = checkDigitConsistency(NUMBERS[j]->Value, NUMBERS[j]->DecimalShift, NUMBERS[j]->analog_roi != NULL, NUMBERS[j]->PreValue);
            }
            else {
                ESP_LOGD(TAG, "checkDigitIncreaseConsistency = true - no digit numbers defined!");
            }
        }
        ESP_LOGD(TAG, "After checkDigitIncreaseConsistency: Value %f", NUMBERS[j]->Value);

        NUMBERS[j]->FlowRateAct = (NUMBERS[j]->Value - NUMBERS[j]->PreValue) / LastPreValueTimeDifference;
        NUMBERS[j]->ReturnRateValue = std::to_string(NUMBERS[j]->FlowRateAct);
        NUMBERS[j]->ReturnChangeAbsolute = RundeOutput(NUMBERS[j]->Value - NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);

        if ((NUMBERS[j]->PreValue + abs(NUMBERS[j]->MaxRateValue)) >= NUMBERS[j]->OverflowValue) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Counter overflow possible?");
            if (NUMBERS[j]->Value <= abs(NUMBERS[j]->MaxRateValue)) {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Counter overflow emerged?");
                NUMBERS[j]->PreValue = 0.0;
            }
        }

        if (PreValueUse && NUMBERS[j]->PreValueOkay && NUMBERS[j]->Value != NUMBERS[j]->PreValue) {
            if (NUMBERS[j]->checkValueIncreaseConsistency) {
                std::string _PreValue = std::to_string(NUMBERS[j]->PreValue);
                std::string _PreValueMax = "";
                std::string _PreValueMin = "";

                if (NUMBERS[j]->MaxRateType == RateChange) {
                    _PreValueMax = std::to_string(NUMBERS[j]->PreValue + (abs(NUMBERS[j]->MaxRateValue) / LastPreValueTimeDifference));
                    _PreValueMin = std::to_string(NUMBERS[j]->PreValue - (abs(NUMBERS[j]->MaxRateValue) / LastPreValueTimeDifference));
                }
                else {
                    _PreValueMax = std::to_string(NUMBERS[j]->PreValue + abs(NUMBERS[j]->MaxRateValue));
                    _PreValueMin = std::to_string(NUMBERS[j]->PreValue - abs(NUMBERS[j]->MaxRateValue));
                }

                int _PreValueMaxSize = 0;
                for (int i = 0; i < _PreValue.length(); ++i) {
                    if (_PreValue[i] == _PreValueMax[i]) {
                        _PreValueMaxSize++;
                    }
                    else {
                        break;
                    }
                }

                int _PreValueMinSize = _PreValueMaxSize;
                if (NUMBERS[j]->AllowNegativeRates) {
                    for (int i = 0; i < _PreValue.length(); ++i) {
                        if (_PreValue[i] == _PreValueMin[i]) {
                            _PreValueMinSize++;
                        }
                        else {
                            break;
                        }
                    }
                }

                int _PreValueSize = min(_PreValueMaxSize, _PreValueMinSize);

                std::string _ValueNew = (_PreValue.substr(0, _PreValueSize)) + std::to_string(NUMBERS[j]->Value).substr(_PreValueSize, _PreValue.length());
                NUMBERS[j]->Value = std::stod(_ValueNew);
            }

            if ((NUMBERS[j]->Nachkomma > 0) && (NUMBERS[j]->ChangeRateThreshold > 0)) {
                double _difference1 = (NUMBERS[j]->PreValue - (NUMBERS[j]->ChangeRateThreshold / pow(10, NUMBERS[j]->Nachkomma)));
                double _difference2 = (NUMBERS[j]->PreValue + (NUMBERS[j]->ChangeRateThreshold / pow(10, NUMBERS[j]->Nachkomma)));

                if ((NUMBERS[j]->Value >= _difference1) && (NUMBERS[j]->Value <= _difference2)) {
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = std::to_string(NUMBERS[j]->PreValue);
                }
            }

            if (NUMBERS[j]->MaxRateType == RateChange) {
                _ratedifference = NUMBERS[j]->FlowRateAct;
            }
            else {
                // TODO:
                // Since I don't know if this is desired, I'll comment it out first.
                // int roundDifference = (int)(round(LastPreValueTimeDifference / LastValueTimeDifference));
                // calculate how many rounds have passed since NUMBERS[j]->timeLastPreValue was set:
                // _ratedifference = ((NUMBERS[j]->Value - NUMBERS[j]->PreValue) / ((int)(round(LastPreValueTimeDifference / LastValueTimeDifference))));
                // Difference per round, as a safeguard in case a reading error(Neg. Rate - Read: or Rate too high - Read:) occurs in the meantime
                _ratedifference = (NUMBERS[j]->Value - NUMBERS[j]->PreValue);
            }

            if ((!NUMBERS[j]->AllowNegativeRates) && (NUMBERS[j]->Value < NUMBERS[j]->PreValue)) {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handle AllowNegativeRate for NUMBERS: " + NUMBERS[j]->name);

                NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "Neg. Rate - Read: " + RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma) + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) +
                                               " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);
                NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                NUMBERS[j]->ReturnValue = "";
                NUMBERS[j]->timeStampLastValue = imagetime;

                std::string temp_bufer = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, temp_bufer);
                WriteDataLog(j);
                continue;
            }

            if ((NUMBERS[j]->useMaxRateValue) && (NUMBERS[j]->Value != NUMBERS[j]->PreValue)) {
                if (abs(_ratedifference) > abs(NUMBERS[j]->MaxRateValue)) {
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handle useMaxRateValue for NUMBERS: " + NUMBERS[j]->name);

                    NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "Rate too high - Read: " + RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma) + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) +
                                                   " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = "";
                    NUMBERS[j]->timeStampLastValue = imagetime;

                    std::string temp_bufer = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, temp_bufer);
                    WriteDataLog(j);
                    continue;
                }
            }
        }

        NUMBERS[j]->PreValue = NUMBERS[j]->Value;
        NUMBERS[j]->PreValueOkay = true;

        NUMBERS[j]->timeStampLastValue = imagetime;
        NUMBERS[j]->timeStampLastPreValue = imagetime;

        NUMBERS[j]->ReturnValue = RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma);
        NUMBERS[j]->ReturnPreValue = RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);

        UpdatePreValueINI = true;

        // NUMBERS[j]->ErrorMessageText = "no error";
        NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "no error - Read: " + RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma) + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) +
                                       " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);

        std::string temp_bufer = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, temp_bufer);
        WriteDataLog(j);
    }

    SavePreValue();
    return true;
}

void ClassFlowPostProcessing::WriteDataLog(int _index)
{
    if (!LogFile.GetDataLogToSD()) {
        return;
    }

    char buffer[80];
    struct tm *timeinfo = localtime(&NUMBERS[_index]->timeStampLastValue);
    strftime(buffer, 80, PREVALUE_TIME_FORMAT_OUTPUT, timeinfo);
    std::string temp_time = std::string(buffer);

    std::string analog = "";
    if (flowAnalog) {
        analog = flowAnalog->getReadoutRawString(_index);
    }

    std::string digit = "";
    if (flowDigit) {
        digit = flowDigit->getReadoutRawString(_index);
    }

    LogFile.WriteToData(temp_time, NUMBERS[_index]->name, NUMBERS[_index]->ReturnRawValue, NUMBERS[_index]->ReturnValue, NUMBERS[_index]->ReturnPreValue, NUMBERS[_index]->ReturnRateValue, NUMBERS[_index]->ReturnChangeAbsolute,
                        NUMBERS[_index]->ErrorMessageText, digit, analog);

    ESP_LOGD(TAG, "WriteDataLog: %s, %s, %s, %s, %s", NUMBERS[_index]->ReturnRawValue.c_str(), NUMBERS[_index]->ReturnValue.c_str(), NUMBERS[_index]->ErrorMessageText.c_str(), digit.c_str(), analog.c_str());
}

void ClassFlowPostProcessing::UpdateNachkommaDecimalShift(void)
{
    for (int j = 0; j < NUMBERS.size(); ++j) {
        // There are only digits
        if (NUMBERS[j]->digit_roi && !NUMBERS[j]->analog_roi) {
            // ESP_LOGD(TAG, "Nurdigit");
            NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShiftInitial;

            // Extended resolution is on and should also be used for this digit.
            if (NUMBERS[j]->isExtendedResolution && flowDigit->isExtendedResolution()) {
                NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShift - 1;
            }

            NUMBERS[j]->Nachkomma = -NUMBERS[j]->DecimalShift;
        }

        if (!NUMBERS[j]->digit_roi && NUMBERS[j]->analog_roi) {
            // ESP_LOGD(TAG, "Nur analog");
            NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShiftInitial;

            if (NUMBERS[j]->isExtendedResolution && flowAnalog->isExtendedResolution()) {
                NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShift - 1;
            }

            NUMBERS[j]->Nachkomma = -NUMBERS[j]->DecimalShift;
        }

        // digit + analog
        if (NUMBERS[j]->digit_roi && NUMBERS[j]->analog_roi) {
            // ESP_LOGD(TAG, "Nur digit + analog");

            NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShiftInitial;
            NUMBERS[j]->Nachkomma = NUMBERS[j]->analog_roi->ROI.size() - NUMBERS[j]->DecimalShift;

            // Extended resolution is on and should also be used for this digit.
            if (NUMBERS[j]->isExtendedResolution && flowAnalog->isExtendedResolution()) {
                NUMBERS[j]->Nachkomma = NUMBERS[j]->Nachkomma + 1;
            }
        }

        ESP_LOGD(TAG, "UpdateNachkommaDecShift NUMBER%i: Nachkomma %i, DecShift %i", j, NUMBERS[j]->Nachkomma, NUMBERS[j]->DecimalShift);
    }
}

std::string ClassFlowPostProcessing::ErsetzteN(std::string input, double _prevalue)
{
    int pot, ziffer;

    int posN = findDelimiterPos(input, "N");
    int posPunkt = findDelimiterPos(input, ".");

    if (posPunkt == std::string::npos) {
        posPunkt = input.length();
    }

    while (posN != std::string::npos) {
        if (posN < posPunkt) {
            pot = posPunkt - posN - 1;
        }
        else {
            pot = posPunkt - posN;
        }

        ziffer = ((int)(_prevalue / pow(10, pot))) % 10;
        input[posN] = ziffer + 48;

        posN = findDelimiterPos(input, "N");
    }

    return input;
}

float ClassFlowPostProcessing::checkDigitConsistency(double input, int _decilamshift, bool _isanalog, double _preValue)
{
    int aktdigit, olddigit;
    int aktdigit_before, olddigit_before;
    bool no_nulldurchgang = false;
    int pot = _decilamshift;

    // if there are no analogue values, the last one cannot be evaluated
    if (!_isanalog) {
        pot++;
    }

#ifdef SERIAL_DEBUG
    ESP_LOGD(TAG, "checkDigitConsistency: pot=%d, decimalshift=%d", pot, _decilamshift);
#endif

    int pot_max = ((int)log10(input)) + 1;

    while (pot <= pot_max) {
        aktdigit_before = ((int)(input / pow(10, pot - 1))) % 10;
        olddigit_before = ((int)(_preValue / pow(10, pot - 1))) % 10;

        aktdigit = ((int)(input / pow(10, pot))) % 10;
        olddigit = ((int)(_preValue / pow(10, pot))) % 10;

        no_nulldurchgang = (olddigit_before <= aktdigit_before);

        if (no_nulldurchgang) {
            if (aktdigit != olddigit) {
                input = input + ((float)(olddigit - aktdigit)) * pow(10, pot); // New Digit is replaced by old Digit;
            }
        }
        else {
            // despite zero crossing, digit was not incremented --> add 1
            if (aktdigit == olddigit) {
                input = input + ((float)(1)) * pow(10, pot); // add 1 at the point
            }
        }

#ifdef SERIAL_DEBUG
        ESP_LOGD(TAG, "checkDigitConsistency: input=%f", input);
#endif

        pot++;
    }

    return input;
}

std::string ClassFlowPostProcessing::getReadout(int _number)
{
    return NUMBERS[_number]->ReturnValue;
}

std::string ClassFlowPostProcessing::getReadoutParam(bool _rawValue, bool _noerror, int _number)
{
    if (_rawValue) {
        return NUMBERS[_number]->ReturnRawValue;
    }

    if (_noerror) {
        return NUMBERS[_number]->ReturnValue;
    }

    return NUMBERS[_number]->ReturnValue;
}

std::string ClassFlowPostProcessing::getReadoutRate(int _number)
{
    return std::to_string(NUMBERS[_number]->FlowRateAct);
}

std::string ClassFlowPostProcessing::getReadoutTimeStamp(int _number)
{
    return NUMBERS[_number]->timeStamp;
}

std::string ClassFlowPostProcessing::getReadoutError(int _number)
{
    return NUMBERS[_number]->ErrorMessageText;
}
