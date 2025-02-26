#pragma once

#ifndef HELPER_H
#define HELPER_H

#include <string>
#include <fstream>
#include <vector>

#include "sdmmc_cmd.h"

#ifdef CONFIG_SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

using namespace std;

/* Error bit fields
   One bit per error
   Make sure it matches https://jomjol.github.io/AI-on-the-edge-device-docs/Error-Codes */
enum SystemStatusFlag_t
{
  // First Byte
  SYSTEM_STATUS_PSRAM_BAD = 1 << 0,        //  1, Critical Error
  SYSTEM_STATUS_HEAP_TOO_SMALL = 1 << 1,   //  2, Critical Error
  SYSTEM_STATUS_CAM_BAD = 1 << 2,          //  4, Critical Error
  SYSTEM_STATUS_SDCARD_CHECK_BAD = 1 << 3, //  8, Critical Error
  SYSTEM_STATUS_FOLDER_CHECK_BAD = 1 << 4, //  16, Critical Error

  // Second Byte
  SYSTEM_STATUS_CAM_FB_BAD = 1 << (0 + 8), //  8, Flow still might work
  SYSTEM_STATUS_NTP_BAD = 1 << (1 + 8),    //  9, Flow will work but time will be wrong
};

static float tsens_value = -1;

#ifdef CONFIG_SOC_TEMP_SENSOR_SUPPORTED
void initTempsensor(void);
#endif
float temperatureRead(void);

std::vector<std::string> HelperZerlegeZeile(std::string input, std::string _delimiter);
std::vector<std::string> ZerlegeZeile(std::string input, std::string delimiter = " =, \t");

std::string FormatFileName(std::string input);
std::size_t file_size(const std::string &file_name);
void findReplace(std::string &line, std::string &oldString, std::string &newString);

bool copyFile(std::string input, std::string output);
bool deleteFile(std::string filepath);
bool deleteFileNormalAndGzip(std::string filepath);
bool renameFile(std::string from, std::string to);
bool renameFolder(std::string from, std::string to);
bool MakeDir(std::string _what);
bool FileExists(std::string filename);
bool FolderExists(std::string foldername);

std::string RundeOutput(double _in, int _anzNachkomma);

size_t findDelimiterPos(std::string input, std::string delimiter);
std::string trim(std::string istring, std::string adddelimiter = "");
bool ctype_space(const char c, std::string adddelimiter);

std::string getFileType(std::string filename);
std::string getFileFullFileName(std::string filename);
std::string getDirectory(std::string filename);

int mkdir_r(const char *dir, const mode_t mode);
int removeFolder(const char *folderPath, const char *logTag);

std::string toLower(std::string in);
std::string toUpper(std::string in);

time_t addDays(time_t startTime, int days);

void memCopyGen(uint8_t *_source, uint8_t *_target, int _size);

size_t getInternalESPHeapSize(void);
size_t getESPHeapSize(void);
std::string getESPHeapInfo(void);

std::string getSDCardPartitionSize(void);
std::string getSDCardFreePartitionSpace(void);
std::string getSDCardPartitionAllocationSize(void);

void SaveSDCardInfo(sdmmc_card_t *card);
std::string SDCardParseManufacturerIDs(int);
std::string getSDCardManufacturer(void);
std::string getSDCardName(void);
std::string getSDCardCapacity(void);
std::string getSDCardSectorSize(void);

std::string getMac(void);

void setSystemStatusFlag(SystemStatusFlag_t flag);
void clearSystemStatusFlag(SystemStatusFlag_t flag);
int getSystemStatus(void);
bool isSetSystemStatusFlag(SystemStatusFlag_t flag);

time_t getUpTime(void);
std::string getResetReason(void);
std::string getFormatedUptime(bool compact);

const char *get404(void);

std::string UrlDecode(const std::string &value);

void replaceAll(std::string &s, const std::string &toReplace, const std::string &replaceWith);
bool replaceString(std::string &s, std::string const &toReplace, std::string const &replaceWith);
bool replaceString(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool logIt);
bool isInString(std::string &s, std::string const &toFind);

bool isStringNumeric(std::string &input);
bool isStringAlphabetic(std::string &input);
bool isStringAlphanumeric(std::string &input);
bool alphanumericToBoolean(std::string &input);

int clipInt(int input, int high, int low);
bool numericStrToBool(std::string input);
bool stringToBoolean(std::string input);

#endif // HELPER_H
