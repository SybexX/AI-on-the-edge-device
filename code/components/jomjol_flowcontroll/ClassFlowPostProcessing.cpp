#include "ClassFlowPostProcessing.h"
#include "Helper.h"
#include "ClassFlowTakeImage.h"
#include "ClassLogFile.h"

#include <iomanip>
#include <sstream>

#include <time.h>

#include "time_sntp.h"

#include "esp_log.h"
#include "../../include/defines.h"

static const char* TAG = "POSTPROC";

std::string ClassFlowPostProcessing::getNumbersName() {
    std::string ret="";

    for (int i = 0; i < NUMBERS.size(); ++i) {
        ret += NUMBERS[i]->name;
	    
        if (i < NUMBERS.size()-1) {
            ret = ret + "\t";
        }
    }

    // ESP_LOGI(TAG, "Result ClassFlowPostProcessing::getNumbersName: %s", ret.c_str());

    return ret;
}

std::string ClassFlowPostProcessing::GetJSON(std::string _lineend) {
    std::string json="{" + _lineend;

    for (int i = 0; i < NUMBERS.size(); ++i) {
        json += "\"" + NUMBERS[i]->name + "\":"  + _lineend;
        json += getJsonFromNumber(i, _lineend) + _lineend;

        if ((i+1) < NUMBERS.size()) {
            json += "," + _lineend;
        }
    }
	
    json += "}";

    return json;
}

string ClassFlowPostProcessing::getJsonFromNumber(int i, std::string _lineend) {
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

string ClassFlowPostProcessing::GetPreValue(std::string _number) {
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

bool ClassFlowPostProcessing::SetPreValue(double _newvalue, string _numbers, bool _extern) {
    //ESP_LOGD(TAG, "SetPrevalue: %f, %s", zw, _numbers.c_str());

    for (int j = 0; j < NUMBERS.size(); ++j) {
        //ESP_LOGD(TAG, "Number %d, %s", j, NUMBERS[j]->name.c_str());
			
        if (NUMBERS[j]->name == _numbers) {
            if (_newvalue >= 0) {  
                // if new value posivive, use provided value to preset PreValue
                NUMBERS[j]->PreValue = _newvalue;
            }
            else {          
                // if new value negative, use last raw value to preset PreValue
                char* p;
                double ReturnRawValueAsDouble = strtod(NUMBERS[j]->ReturnRawValue.c_str(), &p);
		    
                if (ReturnRawValueAsDouble == 0) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SetPreValue: RawValue not a valid value for further processing: " + NUMBERS[j]->ReturnRawValue);
                    return false;
                }
		    
                NUMBERS[j]->PreValue = ReturnRawValueAsDouble;
            }

            NUMBERS[j]->ReturnPreValue = RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);
            NUMBERS[j]->PreValueOkay = true;

            if (_extern) {
                time(&(NUMBERS[j]->timeStampLastPreValue));
                localtime(&(NUMBERS[j]->timeStampLastPreValue));
            }

            //ESP_LOGD(TAG, "Found %d! - set to %.8f", j,  NUMBERS[j]->PreValue);
            
            UpdatePreValueINI = true;   // Only update prevalue file if a new value is set
            SavePreValue();

            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SetPreValue: PreValue for " + NUMBERS[j]->name + " set to " + std::to_string(NUMBERS[j]->PreValue));
            return true;
        }
    }
    
    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SetPreValue: Numbersname not found or not valid");
    return false;   // No new value was set (e.g. wrong numbersname, no numbers at all)
}

bool ClassFlowPostProcessing::LoadPreValue(void) {
    std::vector<string> splitted;
    FILE* pFile;
    char zw[1024];
    string zwtime, zwvalue, name;
    bool _done = false;

    UpdatePreValueINI = false;       // Conversion to the new format

    pFile = fopen(FilePreValue.c_str(), "r");
	
    if (pFile == NULL) {
        return false;
    }

    // Makes sure that an empty file is treated as such.
    zw[0] = '\0';

    fgets(zw, 1024, pFile);
    ESP_LOGD(TAG, "Read line Prevalue.ini: %s", zw);
    zwtime = trim(std::string(zw));
	
    if (zwtime.length() == 0) {
        return false;
    }

    splitted = HelperZerlegeZeile(zwtime, "\t");
	
    //  Conversion to the new format
    if (splitted.size() > 1) {
        while ((splitted.size() > 1) && !_done) {
            name = trim(splitted[0]);
            zwtime = trim(splitted[1]);
            zwvalue = trim(splitted[2]);

            for (int j = 0; j < NUMBERS.size(); ++j) {
                if (NUMBERS[j]->name == name) {
                    NUMBERS[j]->PreValue = stod(zwvalue.c_str());
                    NUMBERS[j]->ReturnPreValue = RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma + 1);      // To be on the safe side, 1 digit more, as Exgtended Resolution may be on (will only be set during the first run).

                    time_t tStart;
                    int yy, month, dd, hh, mm, ss;
                    struct tm whenStart;

                    sscanf(zwtime.c_str(), PREVALUE_TIME_FORMAT_INPUT, &yy, &month, &dd, &hh, &mm, &ss);
                    whenStart.tm_year = yy - 1900;
                    whenStart.tm_mon = month - 1;
                    whenStart.tm_mday = dd;
                    whenStart.tm_hour = hh;
                    whenStart.tm_min = mm;
                    whenStart.tm_sec = ss;
                    whenStart.tm_isdst = -1;

                    NUMBERS[j]->timeStampLastPreValue = mktime(&whenStart);

                    time(&tStart);
                    localtime(&tStart);
                    double difference = difftime(tStart, NUMBERS[j]->timeStampLastPreValue);
                    difference /= 60;
			
                    if (difference > PreValueAgeStartup) {
                        NUMBERS[j]->PreValueOkay = false;
                    }
                    else {
                        NUMBERS[j]->PreValueOkay = true;
                    }
                }
            }

            if (!fgets(zw, 1024, pFile)) {
                _done = true;
            }
            else {
                ESP_LOGD(TAG, "Read line Prevalue.ini: %s", zw);
                splitted = HelperZerlegeZeile(trim(std::string(zw)), "\t");
		    
                if (splitted.size() > 1) {
                    name = trim(splitted[0]);
                    zwtime = trim(splitted[1]);
                    zwvalue = trim(splitted[2]);
                }
            }
        }
        fclose(pFile);
    }   
    else {
        // Old Format
        fgets(zw, 1024, pFile);
        fclose(pFile);
        ESP_LOGD(TAG, "%s", zw);
        zwvalue = trim(std::string(zw));
        NUMBERS[0]->PreValue = stod(zwvalue.c_str());

        time_t tStart;
        int yy, month, dd, hh, mm, ss;
        struct tm whenStart;

        sscanf(zwtime.c_str(), PREVALUE_TIME_FORMAT_INPUT, &yy, &month, &dd, &hh, &mm, &ss);
        whenStart.tm_year = yy - 1900;
        whenStart.tm_mon = month - 1;
        whenStart.tm_mday = dd;
        whenStart.tm_hour = hh;
        whenStart.tm_min = mm;
        whenStart.tm_sec = ss;
        whenStart.tm_isdst = -1;

        ESP_LOGD(TAG, "TIME: %d, %d, %d, %d, %d, %d", whenStart.tm_year, whenStart.tm_mon, whenStart.tm_wday, whenStart.tm_hour, whenStart.tm_min, whenStart.tm_sec);

        NUMBERS[0]->timeStampLastPreValue = mktime(&whenStart);

        time(&tStart);
        localtime(&tStart);
        double difference = difftime(tStart, NUMBERS[0]->timeStampLastPreValue);
        difference /= 60;
			
        if (difference > PreValueAgeStartup) {
            return false;
        }

        NUMBERS[0]->Value = NUMBERS[0]->PreValue;
        NUMBERS[0]->ReturnValue = to_string(NUMBERS[0]->Value);

        if (NUMBERS[0]->digit_roi || NUMBERS[0]->analog_roi) {
            NUMBERS[0]->ReturnValue = RundeOutput(NUMBERS[0]->Value, NUMBERS[0]->Nachkomma);
        }

        UpdatePreValueINI = true;       // Conversion to the new format
        SavePreValue();
    } 

    return true;
}

void ClassFlowPostProcessing::SavePreValue() {
    FILE* pFile;
    string _zw;

    // PreValues unchanged --> File does not have to be rewritten
    if (!UpdatePreValueINI) {
        return;
    }

    pFile = fopen(FilePreValue.c_str(), "w");

    for (int j = 0; j < NUMBERS.size(); ++j) {
        char buffer[80];
        struct tm* timeinfo = localtime(&NUMBERS[j]->timeStampLastPreValue);
        strftime(buffer, 80, PREVALUE_TIME_FORMAT_OUTPUT, timeinfo);
        NUMBERS[j]->timeStamp = std::string(buffer);
        NUMBERS[j]->timeStampTimeUTC = NUMBERS[j]->timeStampLastPreValue;
        // ESP_LOGD(TAG, "SaverPreValue %d, Value: %f, Nachkomma %d", j, NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);

        _zw = NUMBERS[j]->name + "\t" + NUMBERS[j]->timeStamp + "\t" + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) + "\n";
        ESP_LOGD(TAG, "Write PreValue line: %s", _zw.c_str());
			
        if (pFile) {
            fputs(_zw.c_str(), pFile);
        }
    }

    UpdatePreValueINI = false;

    fclose(pFile);
}

ClassFlowPostProcessing::ClassFlowPostProcessing(std::vector<ClassFlow*>* lfc, ClassFlowCNNGeneral *_analog, ClassFlowCNNGeneral *_digit) {
    PreValueUse = false;
    PreValueAgeStartup = 30;
    ErrorMessage = false;
    ListFlowControll = NULL;
    FilePreValue = FormatFileName("/sdcard/config/prevalue.ini");
    ListFlowControll = lfc;
    flowTakeImage = NULL;
    UpdatePreValueINI = false;
    flowAnalog = _analog;
    flowDigit = _digit;

    for (int i = 0; i < ListFlowControll->size(); ++i) {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowTakeImage") == 0) {
            flowTakeImage = (ClassFlowTakeImage*) (*ListFlowControll)[i];
        }
    }
}

void ClassFlowPostProcessing::handleDecimalExtendedResolution(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool _zwdc = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->isExtendedResolution = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handleDecimalSeparator(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        int _zwdc = 0;
	    
        if (isStringNumeric(_value)) {
            _zwdc = std::stoi(_value);
        }

        //  Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->DecimalShift = _zwdc;
            NUMBERS[j]->DecimalShiftInitial = _zwdc;
        }

        NUMBERS[j]->Nachkomma = NUMBERS[j]->AnzahlAnalog - NUMBERS[j]->DecimalShift;
    }
}

void ClassFlowPostProcessing::handleAnalogToDigitTransitionStart(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);
	
    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        float _zwdc = 9.2;
	    
        if (isStringNumeric(_value)) {
            _zwdc = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->AnalogToDigitTransitionStart = _zwdc;

        }
    }
}

void ClassFlowPostProcessing::handleAllowNegativeRate(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);
	
    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }
  
    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool _zwdc = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->AllowNegativeRates = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handleIgnoreLeadingNaN(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool _zwdc = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->IgnoreLeadingNaN = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handleMaxRateType(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);
	
    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        t_RateType _zwdc = AbsoluteChange;

        if (toUpper(_value) == "RATECHANGE") {
            _zwdc = RateChange;
        }

        // Set to default first (if nothing else is set)			
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->MaxRateType = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handleMaxRateValue(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);
	
    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }
	
    for (int j = 0; j < NUMBERS.size(); ++j) {
        float _zwdc = 1;
	    
        if (isStringNumeric(_value)) {
            _zwdc = std::stof(_value);
        }

        // Set to default first (if nothing else is set)			
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->useMaxRateValue = true;
            NUMBERS[j]->MaxRateValue = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handleChangeRateThreshold(string _decsep, string _value) {
    string _digit, _decpos;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        int _zwdc = 2;

        if (isStringNumeric(_value)) {
            _zwdc = std::stof(_value);
        }

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->ChangeRateThreshold = _zwdc;
        }
    }
}

void ClassFlowPostProcessing::handlecheckDigitIncreaseConsistency(std::string _decsep, std::string _value)
{
    std::string _digit;
    int _pospunkt = _decsep.find_first_of(".");
    // ESP_LOGD(TAG, "Name: %s, Pospunkt: %d", _decsep.c_str(), _pospunkt);

    if (_pospunkt > -1) {
        _digit = _decsep.substr(0, _pospunkt);
    }
    else {
        _digit = "default";
    }

    for (int j = 0; j < NUMBERS.size(); ++j) {
        bool _rt = alphanumericToBoolean(_value);

        // Set to default first (if nothing else is set)
        if ((_digit == "default") || (NUMBERS[j]->name == _digit)) {
            NUMBERS[j]->checkDigitIncreaseConsistency = _rt;
        }
    }
}

bool ClassFlowPostProcessing::ReadParameter(FILE* pfile, string& aktparamgraph) {
    std::vector<string> splitted;
    int _n;

    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0) {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }

    // Paragraph does not fit PostProcessing
    if (aktparamgraph.compare("[PostProcessing]") != 0) {
        return false;
    }

    InitNUMBERS();

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph)) {
        splitted = ZerlegeZeile(aktparamgraph);
        std::string _param = GetParameterName(splitted[0]);

        if ((toUpper(_param) == "EXTENDEDRESOLUTION") && (splitted.size() > 1)) {
            handleDecimalExtendedResolution(splitted[0], splitted[1]);
        }

        if ((toUpper(_param) == "DECIMALSHIFT") && (splitted.size() > 1)) {
            handleDecimalSeparator(splitted[0], splitted[1]);
        }
	    
        if ((toUpper(_param) == "ANALOGTODIGITTRANSITIONSTART") && (splitted.size() > 1)) {
            handleAnalogToDigitTransitionStart(splitted[0], splitted[1]);
        }
	    
        if ((toUpper(_param) == "MAXRATEVALUE") && (splitted.size() > 1)) {
            handleMaxRateValue(splitted[0], splitted[1]);
        }
	    
        if ((toUpper(_param) == "MAXRATETYPE") && (splitted.size() > 1)) {
            handleMaxRateType(splitted[0], splitted[1]);
        }
	    
        if ((toUpper(_param) == "PREVALUEUSE") && (splitted.size() > 1)) {
            PreValueUse = alphanumericToBoolean(splitted[1]);
        }
		
        if ((toUpper(_param) == "CHANGERATETHRESHOLD") && (splitted.size() > 1)) {
            handleChangeRateThreshold(splitted[0], splitted[1]);
        }
	    
        if ((toUpper(_param) == "CHECKDIGITINCREASECONSISTENCY") && (splitted.size() > 1)) {
            handlecheckDigitIncreaseConsistency(splitted[0], splitted[1]);
        }
			
        if ((toUpper(_param) == "ALLOWNEGATIVERATES") && (splitted.size() > 1)) {
            handleAllowNegativeRate(splitted[0], splitted[1]);
        }
			
        if ((toUpper(_param) == "ERRORMESSAGE") && (splitted.size() > 1)) {
            ErrorMessage = alphanumericToBoolean(splitted[1]);
        }
			
        if ((toUpper(_param) == "IGNORELEADINGNAN") && (splitted.size() > 1)) {
            handleIgnoreLeadingNaN(splitted[0], splitted[1]);
        }

        if ((toUpper(_param) == "PREVALUEAGESTARTUP") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                PreValueAgeStartup = std::stoi(splitted[1]);
            }
        }
    }

    if (PreValueUse) {
        return LoadPreValue();
    }

    return true;
}

void ClassFlowPostProcessing::InitNUMBERS() {
    int anzDIGIT = 0;
    int anzANALOG = 0;
    std::vector<std::string> name_numbers;

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

        _number->FlowRateAct = 0; // m3 / min
        _number->PreValueOkay = false;
        _number->AllowNegativeRates = false;
        _number->IgnoreLeadingNaN = false;
        _number->MaxRateValue = 0.1;
        _number->MaxRateType = AbsoluteChange;
        _number->useMaxRateValue = false;
        _number->checkDigitIncreaseConsistency = false;
        _number->DecimalShift = 0;
        _number->DecimalShiftInitial = 0;
        _number->isExtendedResolution = false;
        _number->AnalogToDigitTransitionStart=9.2;
        _number->ChangeRateThreshold = 2;

        _number->Value = 0; // last value read out, incl. corrections
        _number->ReturnValue = ""; // corrected return value, possibly with error message
        _number->ReturnRawValue = ""; // raw value (with N & leading 0)    
        _number->PreValue = 0; // last value read out well
        _number->ReturnPreValue = "";
        _number->ErrorMessageText = ""; // Error message for consistency check

        _number->Nachkomma = _number->AnzahlAnalog;

        NUMBERS.push_back(_number);
    }

    for (int i = 0; i < NUMBERS.size(); ++i) {
        ESP_LOGD(TAG, "Number %s, Anz DIG: %d, Anz ANA %d", NUMBERS[i]->name.c_str(), NUMBERS[i]->AnzahlDigit, NUMBERS[i]->AnzahlAnalog);
    }
}

string ClassFlowPostProcessing::ShiftDecimal(string in, int _decShift) {
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

    string zw;
    zw = in.substr(0, _pos_dec_neu);
    zw = zw + ".";
    zw = zw + in.substr(_pos_dec_neu, in.length() - _pos_dec_neu);

    return zw;
}

bool ClassFlowPostProcessing::doFlow(string zwtime) {
    string result = "";
    string digit = "";
    string analog = "";
    string zwvalue;
    string zw;
    time_t imagetime = 0;
    string rohwert;

    // Update decimal point, as the decimal places can also change when changing from CNNType Auto --> xyz:

    imagetime = flowTakeImage->getTimeImageTaken();
	
    if (imagetime == 0) {
        time(&imagetime);
    }

    struct tm* timeinfo;
    timeinfo = localtime(&imagetime);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S", timeinfo);
    zwtime = std::string(strftime_buf);

    ESP_LOGD(TAG, "Quantity NUMBERS: %d", NUMBERS.size());

    for (int j = 0; j < NUMBERS.size(); ++j) {
        NUMBERS[j]->ReturnRawValue = "";
        NUMBERS[j]->ReturnRateValue = "";
        NUMBERS[j]->ReturnValue = "";
        NUMBERS[j]->ReturnChangeAbsolute = RundeOutput(0.0, NUMBERS[j]->Nachkomma); // always reset change absolute
        NUMBERS[j]->ErrorMessageText = "";
        NUMBERS[j]->Value = -1;

        // calculate time difference
        // double LastValueTimeDifference = difftime(imagetime, NUMBERS[j]->timeStampLastValue);         // in seconds
        double LastPreValueTimeDifference = difftime(imagetime, NUMBERS[j]->timeStampLastPreValue);   // in seconds

        UpdateNachkommaDecimalShift();

        int previous_value = -1;

        if (NUMBERS[j]->analog_roi) {
            NUMBERS[j]->ReturnRawValue = flowAnalog->getReadout(j, NUMBERS[j]->isExtendedResolution);
			
            if (NUMBERS[j]->ReturnRawValue.length() > 0) {
                char zw = NUMBERS[j]->ReturnRawValue[0];

                if (zw >= 48 && zw <=57) {
                    previous_value = zw - 48;
                }
            }
        }
		
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After analog->getReadout: ReturnRaw %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif

        if (NUMBERS[j]->digit_roi && NUMBERS[j]->analog_roi) {
            NUMBERS[j]->ReturnRawValue = "." + NUMBERS[j]->ReturnRawValue;
        }

        if (NUMBERS[j]->digit_roi) {
            if (NUMBERS[j]->analog_roi) {
                NUMBERS[j]->ReturnRawValue = flowDigit->getReadout(j, false, previous_value, NUMBERS[j]->analog_roi->ROI[0]->result_float, NUMBERS[j]->AnalogToDigitTransitionStart) + NUMBERS[j]->ReturnRawValue;
            }
            else {
                NUMBERS[j]->ReturnRawValue = flowDigit->getReadout(j, NUMBERS[j]->isExtendedResolution, previous_value);        // Extended Resolution only if there are no analogue digits
            }
        }
	    
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After digit->getReadout: ReturnRaw %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif
	    
        NUMBERS[j]->ReturnRawValue = ShiftDecimal(NUMBERS[j]->ReturnRawValue, NUMBERS[j]->DecimalShift);

        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After ShiftDecimal: ReturnRaw %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif

        if (NUMBERS[j]->IgnoreLeadingNaN) {
            while ((NUMBERS[j]->ReturnRawValue.length() > 1) && (NUMBERS[j]->ReturnRawValue[0] == 'N')) {
                NUMBERS[j]->ReturnRawValue.erase(0, 1);
            }
        }

        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After IgnoreLeadingNaN: ReturnRaw %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif
			
        NUMBERS[j]->ReturnValue = NUMBERS[j]->ReturnRawValue;

        if (findDelimiterPos(NUMBERS[j]->ReturnValue, "N") != std::string::npos) {
            if (PreValueUse && NUMBERS[j]->PreValueOkay) {
                NUMBERS[j]->ReturnValue = ErsetzteN(NUMBERS[j]->ReturnValue, NUMBERS[j]->PreValue); 
            }
            else {
                string _zw = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, _zw);
                NUMBERS[j]->ReturnValue = "";
                NUMBERS[j]->timeStampLastValue = imagetime;
                WriteDataLog(j);
                continue; // there is no number because there is still an N.
            }
        }
			
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After findDelimiterPos: ReturnValue %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif
			
        // Delete leading zeros (unless there is only one 0 left)
        while ((NUMBERS[j]->ReturnValue.length() > 1) && (NUMBERS[j]->ReturnValue[0] == '0')) {
            NUMBERS[j]->ReturnValue.erase(0, 1);
        }
			
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After removeLeadingZeros: ReturnValue %s", NUMBERS[j]->ReturnRawValue.c_str());
        #endif
			
        NUMBERS[j]->Value = std::stod(NUMBERS[j]->ReturnValue);
			
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After setting the Value: Value %f and as double is %f", NUMBERS[j]->Value, std::stod(NUMBERS[j]->ReturnValue));
        #endif

        if (NUMBERS[j]->checkDigitIncreaseConsistency) {
            if (flowDigit) {
                NUMBERS[j]->Value = checkDigitConsistency(NUMBERS[j]->Value, NUMBERS[j]->DecimalShift, NUMBERS[j]->analog_roi != NULL, NUMBERS[j]->PreValue);
            }
            else {
                #ifdef SERIAL_DEBUG
                    ESP_LOGD(TAG, "checkDigitIncreaseConsistency = true - no digit numbers defined!");
                #endif
            }
        }

        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "After checkDigitIncreaseConsistency: Value %f", NUMBERS[j]->Value);
        #endif

        if (PreValueUse && NUMBERS[j]->PreValueOkay) {
            if ((NUMBERS[j]->Nachkomma > 0) && (NUMBERS[j]->ChangeRateThreshold > 0)) {
                double _difference1 = (NUMBERS[j]->PreValue - (NUMBERS[j]->ChangeRateThreshold / pow(10, NUMBERS[j]->Nachkomma)));
                double _difference2 = (NUMBERS[j]->PreValue + (NUMBERS[j]->ChangeRateThreshold / pow(10, NUMBERS[j]->Nachkomma)));

                if ((NUMBERS[j]->Value >= _difference1) && (NUMBERS[j]->Value <= _difference2)) {
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = std::to_string(NUMBERS[j]->PreValue);
                }
            }

            if ((!NUMBERS[j]->AllowNegativeRates) && (NUMBERS[j]->Value < NUMBERS[j]->PreValue)) {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handleAllowNegativeRate for device: " + NUMBERS[j]->name);
					
                if ((NUMBERS[j]->Value < NUMBERS[j]->PreValue)) {
                    // more debug if extended resolution is on, see #2447
                    if (NUMBERS[j]->isExtendedResolution) {
                        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Neg: value=" + std::to_string(NUMBERS[j]->Value) 
                                                    + ", preValue=" + std::to_string(NUMBERS[j]->PreValue) 
                                                    + ", preToll=" + std::to_string(NUMBERS[j]->PreValue-(2/pow(10, NUMBERS[j]->Nachkomma))));
                    } 

                    NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "Neg. Rate - Read: " + zwvalue + " - Raw: " + NUMBERS[j]->ReturnRawValue + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) + " "; 
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = "";
                    NUMBERS[j]->timeStampLastValue = imagetime;

                    string _zw = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, _zw);
                    WriteDataLog(j);
                    continue;
                }
            }

            #ifdef SERIAL_DEBUG
                ESP_LOGD(TAG, "After AllowNegativeRates: Value %f", NUMBERS[j]->Value);
            #endif

            // LastValueTimeDifference = LastValueTimeDifference / 60;       // in minutes
            LastPreValueTimeDifference = LastPreValueTimeDifference / 60; // in minutes
            NUMBERS[j]->FlowRateAct = (NUMBERS[j]->Value - NUMBERS[j]->PreValue) / LastPreValueTimeDifference;
            NUMBERS[j]->ReturnRateValue =  to_string(NUMBERS[j]->FlowRateAct);

            if ((NUMBERS[j]->useMaxRateValue) && (NUMBERS[j]->Value != NUMBERS[j]->PreValue)) {
                double _ratedifference;
					
                if (NUMBERS[j]->MaxRateType == RateChange) {
                    _ratedifference = NUMBERS[j]->FlowRateAct;
                }
                else {
                    // TODO:
                    // Since I don't know if this is desired, I'll comment it out first.
                    // int roundDifference = (int)(round(LastPreValueTimeDifference / LastValueTimeDifference)); // calculate how many rounds have passed since NUMBERS[j]->timeLastPreValue was set
                    // _ratedifference = ((NUMBERS[j]->Value - NUMBERS[j]->PreValue) / ((int)(round(LastPreValueTimeDifference / LastValueTimeDifference)))); // Difference per round, as a safeguard in case a reading error(Neg. Rate - Read: or Rate too high - Read:) occurs in the meantime
                    _ratedifference = (NUMBERS[j]->Value - NUMBERS[j]->PreValue);
                }

                if (abs(_ratedifference) > abs(NUMBERS[j]->MaxRateValue)) {
                    NUMBERS[j]->ErrorMessageText = NUMBERS[j]->ErrorMessageText + "Rate too high - Read: " + RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma) + " - Pre: " + RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma) + " - Rate: " + RundeOutput(_ratedifference, NUMBERS[j]->Nachkomma);
                    NUMBERS[j]->Value = NUMBERS[j]->PreValue;
                    NUMBERS[j]->ReturnValue = "";
                    NUMBERS[j]->ReturnRateValue = "";
                    NUMBERS[j]->timeStampLastValue = imagetime;

                    string _zw = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, _zw);
                    WriteDataLog(j);
                    continue;
                }
            }

        #ifdef SERIAL_DEBUG
           ESP_LOGD(TAG, "After MaxRateCheck: Value %f", NUMBERS[j]->Value);
        #endif
        }
        
        NUMBERS[j]->ReturnChangeAbsolute = RundeOutput(NUMBERS[j]->Value - NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);
        NUMBERS[j]->PreValue = NUMBERS[j]->Value;
        NUMBERS[j]->PreValueOkay = true;

        NUMBERS[j]->timeStampLastValue = imagetime;    
        NUMBERS[j]->timeStampLastPreValue = imagetime;

        NUMBERS[j]->ReturnValue = RundeOutput(NUMBERS[j]->Value, NUMBERS[j]->Nachkomma);
        NUMBERS[j]->ReturnPreValue = RundeOutput(NUMBERS[j]->PreValue, NUMBERS[j]->Nachkomma);

        NUMBERS[j]->ErrorMessageText = "no error";
        UpdatePreValueINI = true;

        string _zw = NUMBERS[j]->name + ": Raw: " + NUMBERS[j]->ReturnRawValue + ", Value: " + NUMBERS[j]->ReturnValue + ", Status: " + NUMBERS[j]->ErrorMessageText;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, _zw);
        WriteDataLog(j);
    }

    SavePreValue();
    return true;
}

void ClassFlowPostProcessing::WriteDataLog(int _index) {
    if (!LogFile.GetDataLogToSD()) {
        return;
    }
    
    string analog = "";
    string digit = "";
    string timezw = "";
    char buffer[80];
    struct tm* timeinfo = localtime(&NUMBERS[_index]->timeStampLastValue);
    strftime(buffer, 80, PREVALUE_TIME_FORMAT_OUTPUT, timeinfo);
    timezw = std::string(buffer);
    
    if (flowAnalog) {
        analog = flowAnalog->getReadoutRawString(_index);
    }

    if (flowDigit) {
        digit = flowDigit->getReadoutRawString(_index);
    }
	
    LogFile.WriteToData(timezw, NUMBERS[_index]->name, NUMBERS[_index]->ReturnRawValue, NUMBERS[_index]->ReturnValue, NUMBERS[_index]->ReturnPreValue, 
        NUMBERS[_index]->ReturnRateValue, NUMBERS[_index]->ReturnChangeAbsolute, NUMBERS[_index]->ErrorMessageText, digit, analog);

    ESP_LOGD(TAG, "WriteDataLog: %s, %s, %s, %s, %s", NUMBERS[_index]->ReturnRawValue.c_str(), NUMBERS[_index]->ReturnValue.c_str(), NUMBERS[_index]->ErrorMessageText.c_str(), digit.c_str(), analog.c_str());
}

void ClassFlowPostProcessing::UpdateNachkommaDecimalShift() {
    for (int j = 0; j < NUMBERS.size(); ++j) {
        // There are only digits
        if (NUMBERS[j]->digit_roi && !NUMBERS[j]->analog_roi) {
            // ESP_LOGD(TAG, "Nurdigit");
            NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShiftInitial;

            // Extended resolution is on and should also be used for this digit.
            if (NUMBERS[j]->isExtendedResolution && flowDigit->isExtendedResolution()) {
                NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShift-1;
            }

            NUMBERS[j]->Nachkomma = -NUMBERS[j]->DecimalShift;
        }

        if (!NUMBERS[j]->digit_roi && NUMBERS[j]->analog_roi) {
            // ESP_LOGD(TAG, "Nur analog");
            NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShiftInitial;
		
            if (NUMBERS[j]->isExtendedResolution && flowAnalog->isExtendedResolution()) {
                NUMBERS[j]->DecimalShift = NUMBERS[j]->DecimalShift-1;
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
                NUMBERS[j]->Nachkomma = NUMBERS[j]->Nachkomma+1;
            }
        }

        ESP_LOGD(TAG, "UpdateNachkommaDecShift NUMBER%i: Nachkomma %i, DecShift %i", j, NUMBERS[j]->Nachkomma,NUMBERS[j]->DecimalShift);
    }
}

string ClassFlowPostProcessing::getReadout(int _number) {
    return NUMBERS[_number]->ReturnValue;
}

string ClassFlowPostProcessing::getReadoutParam(bool _rawValue, bool _noerror, int _number) {
    if (_rawValue) {
        return NUMBERS[_number]->ReturnRawValue;
    }

    if (_noerror) {
        return NUMBERS[_number]->ReturnValue;
    }
	
    return NUMBERS[_number]->ReturnValue;
}

string ClassFlowPostProcessing::ErsetzteN(string input, double _prevalue) {
    int posN, posPunkt;
    int pot, ziffer;
    float zw;

    posN = findDelimiterPos(input, "N");
    posPunkt = findDelimiterPos(input, ".");
	
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

        zw =_prevalue / pow(10, pot);
        ziffer = ((int) zw) % 10;
        input[posN] = ziffer + 48;

        posN = findDelimiterPos(input, "N");
    }

    return input;
}

float ClassFlowPostProcessing::checkDigitConsistency(double input, int _decilamshift, bool _isanalog, double _preValue) {
    int aktdigit, olddigit;
    int aktdigit_before, olddigit_before;
    int pot, pot_max;
    float zw;
    bool no_nulldurchgang = false;

    pot = _decilamshift;

    // if there are no analogue values, the last one cannot be evaluated
    if (!_isanalog) {
        pot++;
    }
	
    #ifdef SERIAL_DEBUG
        ESP_LOGD(TAG, "checkDigitConsistency: pot=%d, decimalshift=%d", pot, _decilamshift);
    #endif
	
    pot_max = ((int) log10(input)) + 1;
	
    while (pot <= pot_max) {
        zw = input / pow(10, pot-1);
        aktdigit_before = ((int) zw) % 10;
        zw = _preValue / pow(10, pot-1);
        olddigit_before = ((int) zw) % 10;

        zw = input / pow(10, pot);
        aktdigit = ((int) zw) % 10;
        zw = _preValue / pow(10, pot);
        olddigit = ((int) zw) % 10;

        no_nulldurchgang = (olddigit_before <= aktdigit_before);

        if (no_nulldurchgang) {
            if (aktdigit != olddigit) {
                input = input + ((float) (olddigit - aktdigit)) * pow(10, pot);     // New Digit is replaced by old Digit;
            }
        }
        else {
            // despite zero crossing, digit was not incremented --> add 1
            if (aktdigit == olddigit) {
                input = input + ((float) (1)) * pow(10, pot);   // add 1 at the point
            }
        }
			
        #ifdef SERIAL_DEBUG
            ESP_LOGD(TAG, "checkDigitConsistency: input=%f", input);
        #endif
			
        pot++;
    }

    return input;
}

string ClassFlowPostProcessing::getReadoutRate(int _number) {
    return std::to_string(NUMBERS[_number]->FlowRateAct);
}

string ClassFlowPostProcessing::getReadoutTimeStamp(int _number) {
   return NUMBERS[_number]->timeStamp; 
}

string ClassFlowPostProcessing::getReadoutError(int _number) {
    return NUMBERS[_number]->ErrorMessageText;
}
