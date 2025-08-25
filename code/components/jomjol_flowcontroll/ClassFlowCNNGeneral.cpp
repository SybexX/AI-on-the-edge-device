#include "ClassFlowCNNGeneral.h"

#include <math.h>
#include <iomanip>
#include <sys/types.h>
#include <sstream>
#include <esp_timer.h>
#include <time_sntp.h>

#include "ClassFlowPostProcessing.h"
#include "ClassControllCamera.h"
#include "ClassFlowTakeImage.h"
#include "ClassFlowControll.h"
#include "CTfLiteClass.h"
#include "ClassLogFile.h"
#include "esp_log.h"

#include "MainFlowControl.h"

#include "defines.h"
#include "Helper.h"

static const char *TAG = "CNN";

// #ifdef CONFIG_HEAP_TRACING_STANDALONE
#ifdef HEAP_TRACING_CLASS_FLOW_CNN_GENERAL_DO_ALING_AND_CUT
#include <esp_heap_trace.h>
#define NUM_RECORDS 300
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

ClassFlowCNNGeneral::ClassFlowCNNGeneral(ClassFlowAlignment *_flowalign, t_CNNType _cnntype) : ClassFlowImage(NULL, TAG)
{
    std::string cnnmodelfile = "";
    modelxsize = 1;
    modelysize = 1;
    CNNGoodThreshold = 0.0;
    ListFlowControll = NULL;
    previousElement = NULL;
    disabled = false;
    isLogImageSelect = false;
    CNNType = AutoDetect;
    CNNType = _cnntype;
    flowpostalignment = _flowalign;
    imagesRetention = 5;
}

std::string ClassFlowCNNGeneral::getReadout(int _analog = 0, bool _extendedResolution, int prev, float _before_narrow_Analog, float AnalogToDigitTransitionStart)
{
    std::string result = "";

    if (GENERAL[_analog]->ROI.size() == 0)
    {
        return result;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "getReadout _analog=" + std::to_string(_analog) + ", _extendedResolution=" + std::to_string(_extendedResolution) + ", prev=" + std::to_string(prev));

    if (CNNType == Analogue || CNNType == Analogue100)
    {
        float number = GENERAL[_analog]->ROI[GENERAL[_analog]->ROI.size() - 1]->result_float;
        int result_after_decimal_point = ((int)floor(number * 10) + 10) % 10;

        prev = PointerEvalAnalogNew(GENERAL[_analog]->ROI[GENERAL[_analog]->ROI.size() - 1]->result_float, prev);

        result = std::to_string(prev);

        if (_extendedResolution)
        {
            result = result + std::to_string(result_after_decimal_point);
        }

        for (int i = GENERAL[_analog]->ROI.size() - 2; i >= 0; --i)
        {
            prev = PointerEvalAnalogNew(GENERAL[_analog]->ROI[i]->result_float, prev);
            result = std::to_string(prev) + result;
        }

        return result;
    }

    if (CNNType == Digit)
    {
        for (int i = 0; i < GENERAL[_analog]->ROI.size(); ++i)
        {
            if ((GENERAL[_analog]->ROI[i]->result_klasse >= 0) && (GENERAL[_analog]->ROI[i]->result_klasse < 10))
            {
                result = result + std::to_string(GENERAL[_analog]->ROI[i]->result_klasse);
            }
            else
            {
                result = result + "N";
            }
        }

        return result;
    }

    if ((CNNType == DoubleHyprid10) || (CNNType == Digit100))
    {
        float number = GENERAL[_analog]->ROI[GENERAL[_analog]->ROI.size() - 1]->result_float;

        if ((number >= 0) && (number < 10))
        {
            // is only set if it is the first digit (no analogue before!)
            if (_extendedResolution)
            {
                int result_after_decimal_point = ((int)floor(number * 10)) % 10;
                int result_before_decimal_point = ((int)floor(number)) % 10;

                result = std::to_string(result_before_decimal_point) + std::to_string(result_after_decimal_point);
                prev = result_before_decimal_point;
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                                    "getReadout(dig100-ext) result_before_decimal_point=" + std::to_string(result_before_decimal_point) + ", result_after_decimal_point=" + std::to_string(result_after_decimal_point) + ", prev=" + std::to_string(prev));
            }
            else
            {
                if (_before_narrow_Analog >= 0)
                {
                    prev = PointerEvalHybridNew(GENERAL[_analog]->ROI[GENERAL[_analog]->ROI.size() - 1]->result_float, _before_narrow_Analog, prev, true, AnalogToDigitTransitionStart);
                }
                else
                {
                    prev = PointerEvalHybridNew(GENERAL[_analog]->ROI[GENERAL[_analog]->ROI.size() - 1]->result_float, prev, prev);
                }

                // is necessary because a number greater than 9.994999 returns a 10! (for further details see check in PointerEvalHybridNew)
                if ((prev >= 0) && (prev < 10))
                {
                    result = std::to_string(prev);
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "getReadout(dig100)  prev=" + std::to_string(prev));
                }
                else
                {
                    result = "N";
                }
            }
        }
        else
        {
            result = "N";
            if (_extendedResolution && (CNNType != Digit))
            {
                result = "NN";
            }
        }

        for (int i = GENERAL[_analog]->ROI.size() - 2; i >= 0; --i)
        {
            if ((GENERAL[_analog]->ROI[i]->result_float >= 0) && (GENERAL[_analog]->ROI[i]->result_float < 10))
            {
                prev = PointerEvalHybridNew(GENERAL[_analog]->ROI[i]->result_float, GENERAL[_analog]->ROI[i + 1]->result_float, prev);
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "getReadout#PointerEvalHybridNew()= " + std::to_string(prev));
                result = std::to_string(prev) + result;
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "getReadout#result= " + result);
            }
            else
            {
                prev = -1;
                result = "N" + result;
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "getReadout(result_float < 0 /'N')  result_float=" + std::to_string(GENERAL[_analog]->ROI[i]->result_float));
            }
        }
        return result;
    }
    return result;
}

/**
 * @brief Determines the number of an ROI in connection with previous ROI results
 *
 * @param number: is the current ROI as float value from recognition
 * @param number_of_predecessors: is the last (lower) ROI as float from recognition
 * @param eval_predecessors: is the evaluated number. Sometimes a much lower value can change higer values
 *                          example: 9.8, 9.9, 0.1
 *                          0.1 => 0 (eval_predecessors)
 *                          The 0 makes a 9.9 to 0 (eval_predecessors)
 *                          The 0 makes a 9.8 to 0
 * @param Analog_Predecessors false/true if the last ROI is an analog or digit ROI (default=false)
 *                              runs in special handling because analog is much less precise
 * @param digitAnalogTransitionStart start of the transitionlogic begins on number_of_predecessor (default=9.2)
 *
 * @return int the determined number of the current ROI
 */
int ClassFlowCNNGeneral::PointerEvalHybridNew(float number, float number_of_predecessors, int eval_predecessors, bool Analog_Predecessors, float digitAnalogTransitionStart)
{
    int result;
    int result_after_decimal_point = ((int)floor(number * 10)) % 10;
    int result_before_decimal_point = ((int)floor(number) + 10) % 10;

    if (eval_predecessors < 0)
    {
        // on first digit is no spezial logic for transition needed
        // we use the recognition as given. The result is the int value of the recognition
        // add precisition of 2 digits and round before trunc
        // a number greater than 9.994999 is returned as 10, this leads to an error during the decimal shift because the
        // NUMBERS[j]->ReturnRawValue is one digit longer. To avoid this, an additional test must be carried out, see "if ((CNNType ==
        // DoubleHyprid10) || (CNNType == Digit100))" check in getReadout() Another alternative would be "result = (int) ((int)
        // trunc(round((number+10 % 10)*1000))) / 1000;", which could, however, lead to other errors?
        result = (int)((int)trunc(round((number + 10 % 10) * 100))) / 100;

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalHybridNew - No predecessor - Result = " + std::to_string(result) + " number: " + std::to_string(number) + " number_of_predecessors = " + std::to_string(number_of_predecessors) +
                                " eval_predecessors = " + std::to_string(eval_predecessors) + " Digit_Uncertainty = " + std::to_string(Digit_Uncertainty));

        return result;
    }

    if (Analog_Predecessors)
    {
        result = PointerEvalAnalogToDigitNew(number, number_of_predecessors, eval_predecessors, digitAnalogTransitionStart);
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalHybridNew - Analog predecessor, evaluation over PointerEvalAnalogNew = " + std::to_string(result) + " number: " + std::to_string(number) + " number_of_predecessors = " + std::to_string(number_of_predecessors) +
                                " eval_predecessors = " + std::to_string(eval_predecessors) + " Digit_Uncertainty = " + std::to_string(Digit_Uncertainty));

        return result;
    }

    if ((number_of_predecessors >= Digit_Transition_Area_Predecessor) && (number_of_predecessors <= (10.0 - Digit_Transition_Area_Predecessor)))
    {
        // no digit change, because predecessor is far enough away (0+/-DigitTransitionRangePredecessor) --> number is rounded
        // Band around the digit --> Round off, as digit reaches inaccuracy in the frame
        if ((result_after_decimal_point <= DigitBand) || (result_after_decimal_point >= (10 - DigitBand)))
        {
            result = ((int)round(number) + 10) % 10;
        }
        else
        {
            result = ((int)trunc(number) + 10) % 10;
        }

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalHybridNew - NO analogue predecessor, no change of digits, as pre-decimal point far enough away = " + std::to_string(result) + " number: " + std::to_string(number) +
                                " number_of_predecessors = " + std::to_string(number_of_predecessors) + " eval_predecessors = " + std::to_string(eval_predecessors) + " Digit_Uncertainty = " + std::to_string(Digit_Uncertainty));

        return result;
    }

    // Zero crossing at the predecessor has taken place (! evaluation via Prev_value and not number!) --> round up here (2.8 --> 3, but
    // also 3.1 --> 3)
    if (eval_predecessors <= 1)
    {
        // We simply assume that the current digit after the zero crossing of the predecessor
        // has passed through at least half (x.5)
        if (result_after_decimal_point > 5)
        {
            // The current digit does not yet have a zero crossing, but the predecessor does..
            result = (result_before_decimal_point + 1) % 10;
        }
        else
        {
            // Act. digit and predecessor have zero crossing
            result = result_before_decimal_point % 10;
        }
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalHybridNew - NO analogue predecessor, zero crossing has taken placen = " + std::to_string(result) + " number: " + std::to_string(number) + " number_of_predecessors = " + std::to_string(number_of_predecessors) +
                                " eval_predecessors = " + std::to_string(eval_predecessors) + " Digit_Uncertainty = " + std::to_string(Digit_Uncertainty));

        return result;
    }

    // remains only >= 9.x --> no zero crossing yet --> 2.8 --> 2,
    // and from 9.7(DigitTransitionRangeLead) 3.1 --> 2
    // everything >=x.4 can be considered as current number in transition. With 9.x predecessor the current
    // number can still be x.6 - x.7.
    // Preceding (else - branch) does not already happen from 9.
    if (Digit_Transition_Area_Forward >= number_of_predecessors || result_after_decimal_point >= 4)
    {
        // The current digit, like the previous digit, does not yet have a zero crossing.
        result = result_before_decimal_point % 10;
    }
    else
    {
        // current digit precedes the smaller digit (9.x). So already >=x.0 while the previous digit has not yet
        // has no zero crossing. Therefore, it is reduced by 1.
        result = (result_before_decimal_point - 1 + 10) % 10;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                        "PointerEvalHybridNew - O analogue predecessor, >= 9.5 --> no zero crossing yet = " + std::to_string(result) + " number: " + std::to_string(number) + " number_of_predecessors = " + std::to_string(number_of_predecessors) +
                            " eval_predecessors = " + std::to_string(eval_predecessors) + " Digit_Uncertainty = " + std::to_string(Digit_Uncertainty) + " result_after_decimal_point = " + std::to_string(result_after_decimal_point));

    return result;
}

int ClassFlowCNNGeneral::PointerEvalAnalogToDigitNew(float number, float numeral_preceder, int eval_predecessors, float AnalogToDigitTransitionStart)
{
    int result;
    int result_after_decimal_point = ((int)floor(number * 10)) % 10;
    int result_before_decimal_point = ((int)floor(number) + 10) % 10;
    bool roundedUp = false;

    // Within the digit inequalities
    // Band around the digit --> Round off, as digit reaches inaccuracy in the frame
    if ((result_after_decimal_point >= (10 - Digit_Uncertainty * 10)) || (eval_predecessors <= 4 && result_after_decimal_point >= 6))
    {
        // or digit runs after (analogue =0..4, digit >=6)
        result = (int)(round(number) + 10) % 10;
        roundedUp = true;

        // before/ after decimal point, because we adjust the number based on the uncertainty.
        result_after_decimal_point = ((int)floor(result * 10)) % 10;
        result_before_decimal_point = ((int)floor(result) + 10) % 10;

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalAnalogToDigitNew - Digit Uncertainty - Result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder: " + std::to_string(numeral_preceder) +
                                " erg before comma: " + std::to_string(result_before_decimal_point) + " erg after comma: " + std::to_string(result_after_decimal_point));
    }
    else
    {
        result = (int)((int)trunc(number) + 10) % 10;
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "PointerEvalAnalogToDigitNew - NO digit Uncertainty - Result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder));
    }

    // No zero crossing has taken place.
    // Only eval_predecessors used because numeral_preceder could be wrong here.
    // numeral_preceder<=0.1 & eval_predecessors=9 corresponds to analogue was reset because of previous analogue that are not yet at 0.
    if ((eval_predecessors >= 6 && (numeral_preceder > AnalogToDigitTransitionStart || numeral_preceder <= 0.2) && roundedUp))
    {
        result = ((result_before_decimal_point + 10) - 1) % 10;

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalAnalogToDigitNew - Nulldurchgang noch nicht stattgefunden = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder) +
                                " eerg after comma = " + std::to_string(result_after_decimal_point));
    }

    return result;
}

int ClassFlowCNNGeneral::PointerEvalAnalogNew(float number, int numeral_preceder)
{
    float number_min, number_max;
    int result;

    if (numeral_preceder == -1)
    {
        result = (int)floor(number);
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                            "PointerEvalAnalogNew - No predecessor - Result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder) +
                                " Analog_error = " + std::to_string(Analog_error));
        return result;
    }

    number_min = number - Analog_error / 10.0;
    number_max = number + Analog_error / 10.0;

    if ((int)floor(number_max) - (int)floor(number_min) != 0)
    {
        if (numeral_preceder <= Analog_error)
        {
            result = ((int)floor(number_max) + 10) % 10;
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                                "PointerEvalAnalogNew - number ambiguous, correction upwards - result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder) +
                                    " Analog_error = " + std::to_string(Analog_error));
            return result;
        }

        if (numeral_preceder >= 10 - Analog_error)
        {
            result = ((int)floor(number_min) + 10) % 10;
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                                "PointerEvalAnalogNew - number ambiguous, downward correction - result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder) +
                                    " Analog_error = " + std::to_string(Analog_error));
            return result;
        }
    }

    result = ((int)floor(number) + 10) % 10;
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                        "PointerEvalAnalogNew - number unambiguous, no correction necessary - result = " + std::to_string(result) + " number: " + std::to_string(number) + " numeral_preceder = " + std::to_string(numeral_preceder) +
                            " Analog_error = " + std::to_string(Analog_error));

    return result;
}

bool ClassFlowCNNGeneral::ReadParameter(FILE *pfile, std::string &aktparamgraph)
{
    aktparamgraph = trim_string_left_right(aktparamgraph);

    if (aktparamgraph.size() == 0)
    {
        if (!this->GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if ((toUpper(aktparamgraph) != "[ANALOG]") && (toUpper(aktparamgraph) != ";[ANALOG]") && (toUpper(aktparamgraph) != "[DIGIT]") && (toUpper(aktparamgraph) != ";[DIGIT]") && (toUpper(aktparamgraph) != "[DIGITS]") &&
        (toUpper(aktparamgraph) != ";[DIGITS]"))
    {
        // Paragraph passt nicht
        return false;
    }

    if (aktparamgraph[0] == ';')
    {
        disabled = true;
        while (getNextLine(pfile, &aktparamgraph) && !isNewParagraph(aktparamgraph))
            ;
        ESP_LOGD(TAG, "[Analog/Digit] is disabled!");

        return true;
    }

    std::vector<std::string> splitted;

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        splitted = splitLine(aktparamgraph);
        std::string _param = toUpper(splitted[0]);

        if (splitted.size() > 1)
        {
            if (_param == "ROIIMAGESLOCATION")
            {
                this->imagesLocation = "/sdcard" + splitted[1];
                this->isLogImage = true;
            }

            else if (_param == "LOGIMAGESELECT")
            {
                LogImageSelect = splitted[1];
                isLogImageSelect = true;
            }

            else if (_param == "ROIIMAGESRETENTION")
            {
                if (isStringNumeric(splitted[1]))
                {
                    this->imagesRetention = std::stoi(splitted[1]);
                }
            }

            else if (_param == "MODEL")
            {
                this->cnnmodelfile = splitted[1];
            }

            else if (_param == "CNNGOODTHRESHOLD")
            {
                if (isStringNumeric(splitted[1]))
                {
                    CNNGoodThreshold = std::stof(splitted[1]);
                }
            }

            else if (splitted.size() >= 5)
            {
                general *_analog = GetGENERAL(splitted[0], true);
                roi *neuroi = _analog->ROI[_analog->ROI.size() - 1];
                neuroi->posx = std::stoi(splitted[1]);
                neuroi->posy = std::stoi(splitted[2]);
                neuroi->deltax = std::stoi(splitted[3]);
                neuroi->deltay = std::stoi(splitted[4]);
                neuroi->CCW = false;

                if (splitted.size() >= 6)
                {
                    neuroi->CCW = toUpper(splitted[5]) == "TRUE";
                }

                neuroi->result_float = -1;
                neuroi->image = NULL;
                neuroi->image_org = NULL;
            }
        }
    }

    if (!getNetworkParameter())
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "An error occured on setting up the Network -> Disabling it!");
        disabled = true; // An error occured, disable this CNN!
        return false;
    }

    for (int _ana = 0; _ana < GENERAL.size(); ++_ana)
    {
        for (int i = 0; i < GENERAL[_ana]->ROI.size(); ++i)
        {
            GENERAL[_ana]->ROI[i]->image = new CImageBasis("ROI " + GENERAL[_ana]->ROI[i]->name, modelxsize, modelysize, modelchannel);
            GENERAL[_ana]->ROI[i]->image_org = new CImageBasis("ROI " + GENERAL[_ana]->ROI[i]->name + " original", GENERAL[_ana]->ROI[i]->deltax, GENERAL[_ana]->ROI[i]->deltay, 3);
        }
    }

    return true;
}

general *ClassFlowCNNGeneral::FindGENERAL(std::string _name_number)
{
    for (int i = 0; i < GENERAL.size(); ++i)
    {
        if (GENERAL[i]->name == _name_number)
        {
            return GENERAL[i];
        }
    }

    return NULL;
}

general *ClassFlowCNNGeneral::GetGENERAL(std::string _name, bool _create = true)
{
    std::string _analog, _roi;
    int _pospunkt = _name.find_first_of(".");

    if (_pospunkt > -1)
    {
        _analog = _name.substr(0, _pospunkt);
        _roi = _name.substr(_pospunkt + 1, _name.length() - _pospunkt - 1);
    }
    else
    {
        _analog = "default";
        _roi = _name;
    }

    general *_ret = NULL;

    for (int i = 0; i < GENERAL.size(); ++i)
    {
        if (GENERAL[i]->name == _analog)
        {
            _ret = GENERAL[i];
        }
    }

    // not found and should not be created
    if (!_create)
    {
        return _ret;
    }

    if (_ret == NULL)
    {
        _ret = new general;
        _ret->name = _analog;
        GENERAL.push_back(_ret);
    }

    roi *neuroi = new roi;
    neuroi->name = _roi;

    _ret->ROI.push_back(neuroi);

    ESP_LOGD(TAG, "GetGENERAL - GENERAL %s - roi %s - CCW: %d", _analog.c_str(), _roi.c_str(), neuroi->CCW);

    return _ret;
}

std::string ClassFlowCNNGeneral::getHTMLSingleStep(std::string host)
{
    std::string result, temp_bufer;
    std::vector<HTMLInfo *> htmlinfo;

    result = "<p>Found ROIs: </p> <p><img src=\"" + host + "/img_tmp/alg_roi.jpg\"></p>\n";
    result = result + "Analog Pointers: <p> ";

    htmlinfo = GetHTMLInfo();

    for (int i = 0; i < htmlinfo.size(); ++i)
    {
        std::stringstream stream;
        stream << std::fixed << std::setprecision(1) << htmlinfo[i]->val;
        temp_bufer = stream.str();

        result = result + "<img src=\"" + host + "/img_tmp/" + htmlinfo[i]->filename + "\"> " + temp_bufer;
        delete htmlinfo[i];
    }

    htmlinfo.clear();

    return result;
}

// wird von ClassFlowControll.cpp -> "bool ClassFlowControll::doFlow(std::string _time)" aufgerufen
bool ClassFlowCNNGeneral::doFlow(std::string _time)
{
#ifdef HEAP_TRACING_CLASS_FLOW_CNN_GENERAL_DO_ALING_AND_CUT
    // register a buffer to record the memory trace
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
    // start tracing
    ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
#endif

    if (disabled)
    {
        return true;
    }

    if (!doAlignAndCut(_time))
    {
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doFlow after alignment");

    doNeuralNetwork(_time);
    RemoveOldLogs();

#ifdef HEAP_TRACING_CLASS_FLOW_CNN_GENERAL_DO_ALING_AND_CUT
    ESP_ERROR_CHECK(heap_trace_stop());
    heap_trace_dump();
#endif

    return true;
}

bool ClassFlowCNNGeneral::doAlignAndCut(std::string _time)
{
    if (disabled)
    {
        return true;
    }

    CAlignAndCutImage *caic = flowpostalignment->GetAlignAndCutImage();

    for (int _number = 0; _number < GENERAL.size(); ++_number)
    {
        for (int _roi = 0; _roi < GENERAL[_number]->ROI.size(); ++_roi)
        {
            ESP_LOGD(TAG, "General %d - Align&Cut", _roi);

            caic->CutAndSave(GENERAL[_number]->ROI[_roi]->posx, GENERAL[_number]->ROI[_roi]->posy, GENERAL[_number]->ROI[_roi]->deltax, GENERAL[_number]->ROI[_roi]->deltay, GENERAL[_number]->ROI[_roi]->image_org);

            if (Camera.SaveAllFiles)
            {
                if (GENERAL[_number]->name == "default")
                {
                    GENERAL[_number]->ROI[_roi]->image_org->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_number]->ROI[_roi]->name + ".jpg"));
                }
                else
                {
                    GENERAL[_number]->ROI[_roi]->image_org->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_number]->name + "_" + GENERAL[_number]->ROI[_roi]->name + ".jpg"));
                }
            }

            GENERAL[_number]->ROI[_roi]->image_org->ResizeImage(modelxsize, modelysize, GENERAL[_number]->ROI[_roi]->image);

            if (Camera.SaveAllFiles)
            {
                if (GENERAL[_number]->name == "default")
                {
                    GENERAL[_number]->ROI[_roi]->image->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_number]->ROI[_roi]->name + ".jpg"));
                }
                else
                {
                    GENERAL[_number]->ROI[_roi]->image->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_number]->name + "_" + GENERAL[_number]->ROI[_roi]->name + ".jpg"));
                }
            }
        }
    }

    return true;
}

void ClassFlowCNNGeneral::DrawROI(CImageBasis *TempImage)
{
    if (TempImage->ImageOkay())
    {
        if (CNNType == Analogue || CNNType == Analogue100)
        {
            int r = 0;
            int g = 255;
            int b = 0;

            for (int _ana = 0; _ana < GENERAL.size(); ++_ana)
            {
                for (int i = 0; i < GENERAL[_ana]->ROI.size(); ++i)
                {
                    TempImage->drawRect(GENERAL[_ana]->ROI[i]->posx, GENERAL[_ana]->ROI[i]->posy, GENERAL[_ana]->ROI[i]->deltax, GENERAL[_ana]->ROI[i]->deltay, r, g, b, 1);
                    TempImage->drawEllipse((int)(GENERAL[_ana]->ROI[i]->posx + GENERAL[_ana]->ROI[i]->deltax / 2), (int)(GENERAL[_ana]->ROI[i]->posy + GENERAL[_ana]->ROI[i]->deltay / 2), (int)(GENERAL[_ana]->ROI[i]->deltax / 2),
                                           (int)(GENERAL[_ana]->ROI[i]->deltay / 2), r, g, b, 2);
                    TempImage->drawLine((int)(GENERAL[_ana]->ROI[i]->posx + GENERAL[_ana]->ROI[i]->deltax / 2), (int)GENERAL[_ana]->ROI[i]->posy, (int)(GENERAL[_ana]->ROI[i]->posx + GENERAL[_ana]->ROI[i]->deltax / 2),
                                        (int)(GENERAL[_ana]->ROI[i]->posy + GENERAL[_ana]->ROI[i]->deltay), r, g, b, 2);
                    TempImage->drawLine((int)GENERAL[_ana]->ROI[i]->posx, (int)(GENERAL[_ana]->ROI[i]->posy + GENERAL[_ana]->ROI[i]->deltay / 2), (int)GENERAL[_ana]->ROI[i]->posx + GENERAL[_ana]->ROI[i]->deltax,
                                        (int)(GENERAL[_ana]->ROI[i]->posy + GENERAL[_ana]->ROI[i]->deltay / 2), r, g, b, 2);
                }
            }
        }
        else
        {
            for (int _dig = 0; _dig < GENERAL.size(); ++_dig)
            {
                for (int i = 0; i < GENERAL[_dig]->ROI.size(); ++i)
                {
                    TempImage->drawRect(GENERAL[_dig]->ROI[i]->posx, GENERAL[_dig]->ROI[i]->posy, GENERAL[_dig]->ROI[i]->deltax, GENERAL[_dig]->ROI[i]->deltay, 0, 0, (255 - _dig * 100), 2);
                }
            }
        }
    }
}

bool ClassFlowCNNGeneral::getNetworkParameter()
{
    if (disabled)
    {
        return true;
    }

    CTfLiteClass *tflite = new CTfLiteClass;
    std::string temp_cnn = "/sdcard" + cnnmodelfile;
    temp_cnn = FormatFileName(temp_cnn);
    ESP_LOGD(TAG, "%s", temp_cnn.c_str());

    if (!tflite->LoadModel(temp_cnn))
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't load tflite model " + cnnmodelfile + " -> Init aborted!");
        LogFile.WriteHeapInfo("getNetworkParameter-LoadModel");
        delete tflite;
        return false;
    }

    if (!tflite->MakeAllocate())
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't allocate tflite model -> Init aborted!");
        LogFile.WriteHeapInfo("getNetworkParameter-MakeAllocate");
        delete tflite;
        return false;
    }

    if (CNNType == AutoDetect)
    {
        tflite->GetInputDimension(false);
        modelxsize = tflite->ReadInputDimenstion(0);
        modelysize = tflite->ReadInputDimenstion(1);
        modelchannel = tflite->ReadInputDimenstion(2);

        int _anzoutputdimensions = tflite->GetAnzOutPut();

        switch (_anzoutputdimensions)
        {
        case 2:
        {
            CNNType = Analogue;
            ESP_LOGD(TAG, "TFlite-Type set to Analogue");
        }
        break;
        case 10:
        {
            CNNType = DoubleHyprid10;
            ESP_LOGD(TAG, "TFlite-Type set to DoubleHyprid10");
        }
        break;
        case 11:
        {
            CNNType = Digit;
            ESP_LOGD(TAG, "TFlite-Type set to Digit");
        }
        break;
        /*
        case 20: {
            CNNType = DigitHyprid10;
            ESP_LOGD(TAG, "TFlite-Type set to DigitHyprid10");
        } break;
        case 22: {
            CNNType = DigitHyprid;
            ESP_LOGD(TAG, "TFlite-Type set to DigitHyprid");
        } break;
        */
        case 100:
        {
            if (modelxsize == 32 && modelysize == 32)
            {
                CNNType = Analogue100;
                ESP_LOGD(TAG, "TFlite-Type set to Analogue100");
            }
            else
            {
                CNNType = Digit100;
                ESP_LOGD(TAG, "TFlite-Type set to Digit");
            }
        }
        break;
        default:
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "tflite does not fit the firmware (outout_dimension=" + std::to_string(_anzoutputdimensions) + ")");
        }
        break;
        }
    }

    delete tflite;
    return true;
}

// wird von "bool ClassFlowCNNGeneral::doFlow(std::string _time)" aufgerufen
bool ClassFlowCNNGeneral::doNeuralNetwork(std::string _time)
{
    if (disabled)
    {
        return true;
    }

    std::string logPath = CreateLogFolder(_time);

    CTfLiteClass *tflite = new CTfLiteClass;
    std::string cnn_model = "/sdcard" + cnnmodelfile;
    cnn_model = FormatFileName(cnn_model);
    ESP_LOGD(TAG, "%s", cnn_model.c_str());

    if (!tflite->LoadModel(cnn_model))
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't load tflite model " + cnnmodelfile + " -> Exec aborted this round!");
        LogFile.WriteHeapInfo("doNeuralNetwork-LoadModel");
        delete tflite;
        return false;
    }

    if (!tflite->MakeAllocate())
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't allocate tfilte model -> Exec aborted this round!");
        LogFile.WriteHeapInfo("doNeuralNetwork-MakeAllocate");
        delete tflite;
        return false;
    }

    std::vector<NumberPost *> numbers = flowctrl.getNumbers();

    time_t _imagetime; // in seconds
    time(&_imagetime);
    localtime(&_imagetime);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _imagetime: " + std::to_string(_imagetime));

    // For each NUMBER
    for (int n = 0; n < GENERAL.size(); ++n)
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Processing Number '" + GENERAL[n]->name + "'");

        int start_roi = 0;

        if ((numbers[n]->useMaxFlowRate) && (numbers[n]->PreValueOkay) && (numbers[n]->timeStampLastValue == numbers[n]->timeStampLastPreValue))
        {
            int _AnzahlDigit = numbers[n]->AnzahlDigit;
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _AnzahlDigit: " + std::to_string(_AnzahlDigit));

            int _AnzahlAnalog = numbers[n]->AnzahlAnalog;
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _AnzahlAnalog: " + std::to_string(_AnzahlAnalog));

            float _MaxFlowRate = (numbers[n]->MaxFlowRate / 60); // in m3/minutes
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _MaxFlowRate: " + std::to_string(_MaxFlowRate) + " m3/min");

            float _LastPreValueTimeDifference = (float)((difftime(_imagetime, numbers[n]->timeStampLastPreValue)) / 60); // in minutes
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _LastPreValueTimeDifference: " + std::to_string(_LastPreValueTimeDifference) + " minutes");

            std::string _PreValue_old = std::to_string((float)numbers[n]->PreValue);
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _PreValue_old: " + _PreValue_old);

            ////////////////////////////////////////////////////
            std::string _PreValue_new1 = std::to_string((float)numbers[n]->PreValue + (_MaxFlowRate * _LastPreValueTimeDifference));
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _PreValue_new1: " + _PreValue_new1);

            std::string _PreValue_new2 = std::to_string((float)numbers[n]->PreValue - (_MaxFlowRate * _LastPreValueTimeDifference));
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _PreValue_new2: " + _PreValue_new2);

            ////////////////////////////////////////////////////
            // is necessary because there are always 6 numbers after the DecimalPoint due to float
            int _CorrectionValue = 0;
            int _pospunkt = _PreValue_old.find_first_of(".");
            if (_pospunkt > -1)
            {
                _CorrectionValue = _PreValue_old.length() - _pospunkt;
                _CorrectionValue = _CorrectionValue - numbers[n]->Nachkomma;
            }
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _CorrectionValue: " + std::to_string(_CorrectionValue));

            int _PreValue_len = ((int)_PreValue_old.length() - _CorrectionValue);
            if (numbers[n]->isExtendedResolution)
            {
                _PreValue_len = _PreValue_len - 1;
            }
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "doNeuralNetwork, _PreValue_len(without DecimalPoint and ExtendedResolution): " + std::to_string(_PreValue_len));

            ////////////////////////////////////////////////////
            // (+) Find out which Numbers should not change
            int _DecimalPoint1 = 0;
            int _NumbersNotChanged1 = 0;
            while ((_PreValue_old.length() > _NumbersNotChanged1) && (_PreValue_old[_NumbersNotChanged1] == _PreValue_new1[_NumbersNotChanged1]))
            {
                if (_PreValue_old[_NumbersNotChanged1] == '.')
                {
                    _DecimalPoint1 = 1;
                }
                _NumbersNotChanged1++;
            }
            _NumbersNotChanged1 = _NumbersNotChanged1 - _DecimalPoint1;
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Number of ROIs that should not change: " + std::to_string(_NumbersNotChanged1));

            if ((_AnzahlDigit + _AnzahlAnalog) > _PreValue_len)
            {
                _NumbersNotChanged1 = _NumbersNotChanged1 + ((_AnzahlDigit + _AnzahlAnalog) - _PreValue_len);
            }
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Number of ROIs that should not change(corrected): " + std::to_string(_NumbersNotChanged1));

            ////////////////////////////////////////////////////
            // (-) Find out which Numbers should not change
            int _NumbersNotChanged2 = _NumbersNotChanged1;
            if (numbers[n]->AllowNegativeRates)
            {
                int _DecimalPoint2 = 0;
                while ((_PreValue_old.length() > _NumbersNotChanged2) && (_PreValue_old[_NumbersNotChanged2] == _PreValue_new2[_NumbersNotChanged2]))
                {
                    if (_PreValue_old[_NumbersNotChanged2] == '.')
                    {
                        _DecimalPoint2 = 1;
                    }
                    _NumbersNotChanged2++;
                }
                _NumbersNotChanged2 = _NumbersNotChanged2 - _DecimalPoint2;
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Number of ROIs that should not change: " + std::to_string(_NumbersNotChanged2));

                if ((_AnzahlDigit + _AnzahlAnalog) > _PreValue_len)
                {
                    _NumbersNotChanged2 = _NumbersNotChanged2 + ((_AnzahlDigit + _AnzahlAnalog) - _PreValue_len);
                }
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Number of ROIs that should not change(corrected): " + std::to_string(_NumbersNotChanged2));
            }

            ////////////////////////////////////////////////////
            int start_digit_new = 0;
            int start_analog_new = 0;
            int _NumbersNotChanged = min(_NumbersNotChanged1, _NumbersNotChanged2);
            if (_NumbersNotChanged <= _AnzahlDigit)
            {
                // The change already takes place at the digit ROIs
                start_digit_new = (_AnzahlDigit - _NumbersNotChanged);
                start_digit_new = (_AnzahlDigit - start_digit_new);
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "From the " + std::to_string(start_digit_new) + " th digit ROI is evaluated");
            }
            else
            {
                // The change only takes place at the analog ROIs
                start_digit_new = _AnzahlDigit;
                start_analog_new = (_AnzahlAnalog - (_NumbersNotChanged - _AnzahlDigit));
                start_analog_new = (_AnzahlAnalog - start_analog_new);
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "From the " + std::to_string(start_analog_new) + " th analog ROI is evaluated");
            }

            ////////////////////////////////////////////////////
            if (CNNType == Digit || CNNType == Digit100 || CNNType == DoubleHyprid10)
            {
                start_roi = start_digit_new;
            }
            else if (CNNType == Analogue || CNNType == Analogue100)
            {
                start_roi = start_analog_new;
            }
        }

        // For each ROI
        for (int roi = start_roi; roi < GENERAL[n]->ROI.size(); ++roi)
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "ROI #" + std::to_string(roi) + " - TfLite");
            // ESP_LOGD(TAG, "General %d - TfLite", i);

            switch (CNNType)
            {
            case Analogue:
            {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CNN Type: Analogue");
                float f1, f2;
                f1 = 0;
                f2 = 0;

                tflite->LoadInputImageBasis(GENERAL[n]->ROI[roi]->image);
                tflite->Invoke();
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "After Invoke");

                f1 = tflite->GetOutputValue(0);
                f2 = tflite->GetOutputValue(1);
                float result = fmod(atan2(f1, f2) / (M_PI * 2) + 2, 1);

                if (GENERAL[n]->ROI[roi]->CCW)
                {
                    GENERAL[n]->ROI[roi]->result_float = 10 - (result * 10);
                }
                else
                {
                    GENERAL[n]->ROI[roi]->result_float = result * 10;
                }

                ESP_LOGD(TAG, "General result (Analog)%i - CCW: %d -  %f", roi, GENERAL[n]->ROI[roi]->CCW, GENERAL[n]->ROI[roi]->result_float);
                if (isLogImage)
                {
                    LogImage(logPath, GENERAL[n]->ROI[roi]->name, &GENERAL[n]->ROI[roi]->result_float, NULL, _time, GENERAL[n]->ROI[roi]->image_org);
                }
            }
            break;

            case Digit:
            {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CNN Type: Digit");
                GENERAL[n]->ROI[roi]->result_klasse = 0;
                GENERAL[n]->ROI[roi]->result_klasse = tflite->GetClassFromImageBasis(GENERAL[n]->ROI[roi]->image);
                ESP_LOGD(TAG, "General result (Digit)%i: %d", roi, GENERAL[n]->ROI[roi]->result_klasse);

                if (isLogImage)
                {
                    std::string _imagename = GENERAL[n]->name + "_" + GENERAL[n]->ROI[roi]->name;
                    if (isLogImageSelect)
                    {
                        if (LogImageSelect.find(GENERAL[n]->ROI[roi]->name) != std::string::npos)
                        {
                            LogImage(logPath, _imagename, NULL, &GENERAL[n]->ROI[roi]->result_klasse, _time, GENERAL[n]->ROI[roi]->image_org);
                        }
                    }
                    else
                    {
                        LogImage(logPath, _imagename, NULL, &GENERAL[n]->ROI[roi]->result_klasse, _time, GENERAL[n]->ROI[roi]->image_org);
                    }
                }
            }
            break;

            case DoubleHyprid10:
            {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CNN Type: DoubleHyprid10");
                int _num, _numplus, _numminus;
                float _val, _valplus, _valminus;
                float _fit;
                float _result_save_file;

                tflite->LoadInputImageBasis(GENERAL[n]->ROI[roi]->image);
                tflite->Invoke();
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "After Invoke");

                _num = tflite->GetOutClassification(0, 9);
                _numplus = (_num + 1) % 10;
                _numminus = (_num - 1 + 10) % 10;

                _val = tflite->GetOutputValue(_num);
                _valplus = tflite->GetOutputValue(_numplus);
                _valminus = tflite->GetOutputValue(_numminus);

                float result = _num;

                if (_valplus > _valminus)
                {
                    result = result + _valplus / (_valplus + _val);
                    _fit = _val + _valplus;
                }
                else
                {
                    result = result - _valminus / (_val + _valminus);
                    _fit = _val + _valminus;
                }

                if (result >= 10)
                {
                    result = result - 10;
                }

                if (result < 0)
                {
                    result = result + 10;
                }

                std::string temp_bufer = "_num (p, m): " + std::to_string(_num) + " " + std::to_string(_numplus) + " " + std::to_string(_numminus);
                temp_bufer = temp_bufer + " _val (p, m): " + std::to_string(_val) + " " + std::to_string(_valplus) + " " + std::to_string(_valminus);
                temp_bufer = temp_bufer + " result: " + std::to_string(result) + " _fit: " + std::to_string(_fit);
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, temp_bufer);

                _result_save_file = result;

                if (_fit < CNNGoodThreshold)
                {
                    GENERAL[n]->ROI[roi]->isReject = true;
                    result = -1;
                    _result_save_file += 100; // In case fit is not sufficient, the result should still be saved with "-10x.y".
                    std::string temp_bufer = "Value Rejected due to Threshold (Fit: " + std::to_string(_fit) + ", Threshold: " + std::to_string(CNNGoodThreshold) + ")";
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, temp_bufer);
                }
                else
                {
                    GENERAL[n]->ROI[roi]->isReject = false;
                }

                GENERAL[n]->ROI[roi]->result_float = result;
                ESP_LOGD(TAG, "Result General(Analog)%i: %f", roi, GENERAL[n]->ROI[roi]->result_float);

                if (isLogImage)
                {
                    std::string _imagename = GENERAL[n]->name + "_" + GENERAL[n]->ROI[roi]->name;
                    if (isLogImageSelect)
                    {
                        if (LogImageSelect.find(GENERAL[n]->ROI[roi]->name) != std::string::npos)
                        {
                            LogImage(logPath, _imagename, &_result_save_file, NULL, _time, GENERAL[n]->ROI[roi]->image_org);
                        }
                    }
                    else
                    {
                        LogImage(logPath, _imagename, &_result_save_file, NULL, _time, GENERAL[n]->ROI[roi]->image_org);
                    }
                }
            }
            break;
            case Digit100:
            case Analogue100:
            {
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CNN Type: Digit100 or Analogue100");
                int _num;
                float _result_save_file;

                tflite->LoadInputImageBasis(GENERAL[n]->ROI[roi]->image);
                tflite->Invoke();

                _num = tflite->GetOutClassification();

                if (GENERAL[n]->ROI[roi]->CCW)
                {
                    GENERAL[n]->ROI[roi]->result_float = 10 - ((float)_num / 10.0);
                }
                else
                {
                    GENERAL[n]->ROI[roi]->result_float = (float)_num / 10.0;
                }

                _result_save_file = GENERAL[n]->ROI[roi]->result_float;
                GENERAL[n]->ROI[roi]->isReject = false;
                ESP_LOGD(TAG, "Result General(Analog)%i - CCW: %d -  %f", roi, GENERAL[n]->ROI[roi]->CCW, GENERAL[n]->ROI[roi]->result_float);

                if (isLogImage)
                {
                    std::string _imagename = GENERAL[n]->name + "_" + GENERAL[n]->ROI[roi]->name;
                    if (isLogImageSelect)
                    {
                        if (LogImageSelect.find(GENERAL[n]->ROI[roi]->name) != std::string::npos)
                        {
                            LogImage(logPath, _imagename, &_result_save_file, NULL, _time, GENERAL[n]->ROI[roi]->image_org);
                        }
                    }
                    else
                    {
                        LogImage(logPath, _imagename, &_result_save_file, NULL, _time, GENERAL[n]->ROI[roi]->image_org);
                    }
                }
            }
            break;

            default:
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CNN Type: unknown");
            }
            break;
            }
        }
    }

    delete tflite;
    return true;
}

bool ClassFlowCNNGeneral::isExtendedResolution(int _number)
{
    if (CNNType == Digit)
    {
        return false;
    }

    return true;
}

std::vector<HTMLInfo *> ClassFlowCNNGeneral::GetHTMLInfo()
{
    std::vector<HTMLInfo *> result;

    for (int _ana = 0; _ana < GENERAL.size(); ++_ana)
    {
        for (int i = 0; i < GENERAL[_ana]->ROI.size(); ++i)
        {
            ESP_LOGD(TAG, "Image: %d", (int)GENERAL[_ana]->ROI[i]->image);

            if (GENERAL[_ana]->ROI[i]->image)
            {
                if (GENERAL[_ana]->name == "default")
                {
                    GENERAL[_ana]->ROI[i]->image->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_ana]->ROI[i]->name + ".jpg"));
                }
                else
                {
                    GENERAL[_ana]->ROI[i]->image->SaveToFile(FormatFileName("/sdcard/img_tmp/" + GENERAL[_ana]->name + "_" + GENERAL[_ana]->ROI[i]->name + ".jpg"));
                }
            }

            HTMLInfo *temp_bufer = new HTMLInfo;
            if (GENERAL[_ana]->name == "default")
            {
                temp_bufer->filename = GENERAL[_ana]->ROI[i]->name + ".jpg";
                temp_bufer->filename_org = GENERAL[_ana]->ROI[i]->name + ".jpg";
            }
            else
            {
                temp_bufer->filename = GENERAL[_ana]->name + "_" + GENERAL[_ana]->ROI[i]->name + ".jpg";
                temp_bufer->filename_org = GENERAL[_ana]->name + "_" + GENERAL[_ana]->ROI[i]->name + ".jpg";
            }

            if (CNNType == Digit)
            {
                temp_bufer->val = GENERAL[_ana]->ROI[i]->result_klasse;
            }
            else
            {
                temp_bufer->val = GENERAL[_ana]->ROI[i]->result_float;
            }

            temp_bufer->image = GENERAL[_ana]->ROI[i]->image;
            temp_bufer->image_org = GENERAL[_ana]->ROI[i]->image_org;

            result.push_back(temp_bufer);
        }
    }

    return result;
}

int ClassFlowCNNGeneral::getNumberGENERAL()
{
    return GENERAL.size();
}

std::string ClassFlowCNNGeneral::getNameGENERAL(int _roi)
{
    if (_roi < GENERAL.size())
    {
        return GENERAL[_roi]->name;
    }

    return "GENERAL DOES NOT EXIST";
}

general *ClassFlowCNNGeneral::GetGENERAL(int _roi)
{
    if (_roi < GENERAL.size())
    {
        return GENERAL[_roi];
    }

    return NULL;
}

void ClassFlowCNNGeneral::UpdateNameNumbers(std::vector<std::string> *_name_numbers)
{
    for (int _roi = 0; _roi < GENERAL.size(); _roi++)
    {
        std::string _name = GENERAL[_roi]->name;
        bool found = false;

        for (int i = 0; i < (*_name_numbers).size(); ++i)
        {
            if ((*_name_numbers)[i] == _name)
            {
                found = true;
            }
        }
        if (!found)
        {
            (*_name_numbers).push_back(_name);
        }
    }
}

std::string ClassFlowCNNGeneral::getReadoutRawString(int _roi)
{
    std::string rt = "";

    if (_roi >= GENERAL.size() || GENERAL[_roi] == NULL || GENERAL[_roi]->ROI.size() == 0)
    {
        return rt;
    }

    for (int i = 0; i < GENERAL[_roi]->ROI.size(); ++i)
    {
        if (CNNType == Analogue || CNNType == Analogue100)
        {
            rt = rt + "," + RundeOutput(GENERAL[_roi]->ROI[i]->result_float, 1);
        }

        if (CNNType == Digit)
        {
            if (GENERAL[_roi]->ROI[i]->result_klasse >= 10)
            {
                rt = rt + ",N";
            }
            else
            {
                rt = rt + "," + RundeOutput(GENERAL[_roi]->ROI[i]->result_klasse, 0);
            }
        }

        if ((CNNType == DoubleHyprid10) || (CNNType == Digit100))
        {
            rt = rt + "," + RundeOutput(GENERAL[_roi]->ROI[i]->result_float, 1);
        }
    }

    return rt;
}
