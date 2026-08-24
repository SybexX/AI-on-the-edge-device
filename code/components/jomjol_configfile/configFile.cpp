#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Helper.h"
#include "configFile.h"
#include <esp_log.h>

#include "../../include/defines.h"

static const char *TAG = "CONFIG";

ConfigFile::ConfigFile(std::string filePath)
{
    std::string config = FormatFileName(filePath);
    pFile = fopen(config.c_str(), "r");
}

ConfigFile::~ConfigFile()
{
    if (pFile != NULL)
        fclose(pFile);
}

bool ConfigFile::isNewParagraph(const std::string& input)
{
    if (input.empty())
        return false;

    if (input[0] == '[')
        return true;

    return input.size() >= 2 && input[0] == ';' && input[1] == '[';
}

bool ConfigFile::GetNextParagraph(std::string& aktparamgraph, bool &disabled, bool &eof)
{
	while (getNextLine(&aktparamgraph, disabled, eof) && !isNewParagraph(aktparamgraph) && !eof);

	if (isNewParagraph(aktparamgraph))
		return true;
	
	return false;
}

bool ConfigFile::getNextLine(std::string *rt, bool &disabled, bool &eof)
{
    eof = false;
	char temp_char[256] = "";
	if (pFile == NULL)
	{
		*rt = "";
		return false;
	}

	if (fgets(temp_char, sizeof(temp_char), pFile))
	{
		ESP_LOGD(TAG, "%s", temp_char);
		if ((strlen(temp_char) == 0) && feof(pFile))
		{
			*rt = "";
			eof = true;
			return false;
		}
	}
	else
	{
		*rt = "";
		eof = true;
		return false;
	}
	*rt = temp_char;
	*rt = trim(*rt);
	while ((temp_char[0] == ';' || temp_char[0] == '#' || (rt->size() == 0)) && !(temp_char[1] == '['))
	{
		fgets(temp_char, sizeof(temp_char), pFile);
		ESP_LOGD(TAG, "%s", temp_char);
		if (feof(pFile))
		{
			*rt = "";
            eof = true;
			return false;
		}
		*rt = temp_char;
		*rt = trim(*rt);
	}

    disabled = ((*rt)[0] == ';');
	return true;
}
