#include "ClassFlow.h"
#include <fstream>
#include <string>
#include <iostream>
#include <string.h>
#include "esp_log.h"
#include "../../include/defines.h"

static const char *TAG = "CLASS";

void ClassFlow::SetInitialParameter(void)
{
	ListFlowControll = NULL;
	previousElement = NULL;	
	disabled = false;
}

bool ClassFlow::isNewParagraph(const std::string& input)
{
    if (input.empty())
	{
        return false;
	}

    if (input[0] == '[')
	{
        return true;
	}

    return input.size() >= 2 && input[0] == ';' && input[1] == '[';
}

bool ClassFlow::GetNextParagraph(FILE* pfile, std::string& aktparamgraph)
{
	while (getNextLine(pfile, &aktparamgraph) && !isNewParagraph(aktparamgraph));

	if (isNewParagraph(aktparamgraph))
	{
		return true;
	}
	
	return false;
}

ClassFlow::ClassFlow(void)
{
	SetInitialParameter();
}

ClassFlow::ClassFlow(std::vector<ClassFlow*> * lfc)
{
	SetInitialParameter();	
	ListFlowControll = lfc;
}

ClassFlow::ClassFlow(std::vector<ClassFlow*> * lfc, ClassFlow *_prev)
{
	SetInitialParameter();	
	ListFlowControll = lfc;
	previousElement = _prev;
}	

bool ClassFlow::ReadParameter(FILE* pfile, std::string &aktparamgraph)
{
	return false;
}

bool ClassFlow::doFlow(std::string time)
{
	return false;
}

std::string ClassFlow::getHTMLSingleStep(std::string host)
{
	return "";
}

std::string ClassFlow::GetParameterName(std::string _input)
{
    std::string _param;
    int _pospunkt = _input.find_first_of(".");
	
    if (_pospunkt > -1)
    {
        _param = _input.substr(_pospunkt+1, _input.length() - _pospunkt - 1);
    }
    else
    {
        _param = _input;
    }
	
	return _param;
}

bool ClassFlow::getNextLine(FILE* pfile, std::string *rt)
{
	if (pfile == NULL)
	{
		*rt = "";
		return false;
	}

	char temp_char[256] = "";
	if (fgets(temp_char, sizeof(temp_char), pfile) == NULL)
	{
		*rt = "";
		return false;
	}
	
	ESP_LOGD(TAG, "%s", temp_char);
	*rt = temp_char;
	*rt = trim(*rt);
	
	while ((temp_char[0] == ';' || temp_char[0] == '#' || (rt->size() == 0)) && !(temp_char[1] == '['))
	{
		if (fgets(temp_char, sizeof(temp_char), pfile) == NULL)
		{
			*rt = "";
			return false;
		}
		
		ESP_LOGD(TAG, "%s", temp_char);
		*rt = temp_char;
		*rt = trim(*rt);
	}
	
	return true;
}
