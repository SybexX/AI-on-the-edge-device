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

bool ClassFlow::isNewParagraph(string input)
{
	if ((input[0] == '[') || ((input[0] == ';') && (input[1] == '[')))
	{
		return true;
	}
	
	return false;
}

bool ClassFlow::GetNextParagraph(FILE* pfile, string& aktparamgraph)
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

bool ClassFlow::ReadParameter(FILE* pfile, string &aktparamgraph)
{
	return false;
}

bool ClassFlow::doFlow(string time)
{
	return false;
}

string ClassFlow::getHTMLSingleStep(string host)
{
	return "";
}

std::string ClassFlow::GetParameterName(std::string _input)
{
    string _param;
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

bool ClassFlow::getNextLine(FILE* pfile, string *rt)
{
	char temp_char[256] = "";
	
	if (pfile == NULL)
	{
		*rt = "";
		return false;
	}
	
	if (!fgets(temp_char, sizeof(temp_char), pfile))
	{
		*rt = "";
		ESP_LOGD(TAG, "END OF FILE");
		return false;
	}
	
	ESP_LOGD(TAG, "%s", temp_char);
	
	*rt = temp_char;
	*rt = trim(*rt);
	
	while ((temp_char[0] == ';' || temp_char[0] == '#' || (rt->size() == 0)) && !(temp_char[1] == '['))
	{
		*rt = "";
		
		if (!fgets(temp_char, sizeof(temp_char), pfile))
		{
			return false;
		}
		
		ESP_LOGD(TAG, "%s", temp_char);
		*rt = temp_char;
		*rt = trim(*rt);
	}
	
	return true;
}
