#pragma once

#ifndef CLASSFLOWCNNGENERAL_H
#define CLASSFLOWCNNGENERAL_H

#include"ClassFlowDefineTypes.h"
#include "ClassFlowAlignment.h"


enum t_CNNType {
    AutoDetect,
    Analogue,
    Analogue100,
    Digit,
    DigitHyprid10,
    DoubleHyprid10,
    Digit100,
    None
 };

class ClassFlowCNNGeneral :
    public ClassFlowImage
{
protected:
    t_CNNType CNNType;
    std::vector<general*> GENERAL;
    float CNNGoodThreshold;

    string cnnmodelfile;
    int modelxsize, modelysize, modelchannel;
    bool isLogImageSelect;
    string LogImageSelect;
    ClassFlowAlignment* flowpostalignment;

    bool SaveAllFiles;   

    int PointerEvalAnalogNew(float zahl, int numeral_preceder);
    int PointerEvalAnalogToDigitNew(float zahl, float numeral_preceder,  int eval_predecessors, float AnalogToDigitTransitionStart);
    int PointerEvalHybridNew(float zahl, float number_of_predecessors, int eval_predecessors, bool Analog_Predecessors = false, float AnalogToDigitTransitionStart=9.2);



    bool doNeuralNetwork(string time); 
    bool doAlignAndCut(string time);

    bool getNetworkParameter();

public:
    ClassFlowCNNGeneral(ClassFlowAlignment *_flowalign, t_CNNType _cnntype = AutoDetect);

    bool ReadParameter(FILE* pfile, string& aktparamgraph);
    bool doFlow(string time);

    string getHTMLSingleStep(string host);
    string getReadout(int _analog, bool _extendedResolution = false, int prev = -1, float _before_narrow_Analog = -1, float AnalogToDigitTransitionStart=9.2); 

    string getReadoutRawString(int _analog);  

    void DrawROI(CImageBasis *_zw); 

   	std::vector<HTMLInfo*> GetHTMLInfo();   

    int getNumberGENERAL();
    general* GetGENERAL(int _analog);
    general* GetGENERAL(string _name, bool _create);
    general* FindGENERAL(string _name_number);    
    string getNameGENERAL(int _analog);    

    bool isExtendedResolution(int _number = 0);

    void UpdateNameNumbers(std::vector<std::string> *_name_numbers);

    t_CNNType getCNNType(){return CNNType;};

    string name(){return "ClassFlowCNNGeneral";}; 
};

#endif

