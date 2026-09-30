#pragma once

#ifndef HELPER_H
#define HELPER_H

#include <fstream>
#include <vector>
#include <string.h>
#include "sdmmc_cmd.h"

using namespace std;

/* Error bit fields
   One bit per error
   Make sure it matches https://jomjol.github.io/AI-on-the-edge-device-docs/Error-Codes */
enum SystemStatusFlag_t
{                                           // One bit per error
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

constexpr float RAD_TO_DEG = 57.29577951308232f;
constexpr float DEG_TO_RAD = 0.017453292519943295f;

std::string FormatFileName(std::string input);
std::size_t file_size(const std::string &file_name);
void FindReplace(std::string &line, std::string &oldString, std::string &newString);

bool CopyFile(string input, string output);
bool DeleteFile(string filename);
bool RenameFile(string from, string to);
bool RenameFolder(string from, string to);
bool MakeDir(std::string _what);
bool FileExists(string filename);
bool FolderExists(string foldername);

string RundeOutput(double _in, int _anzNachkomma);

size_t findDelimiterPos(string input, string delimiter);
string trim(string istring, string adddelimiter = "");
bool ctype_space(const char c, string adddelimiter);

string getFileType(string filename);
string getFileFullFileName(string filename);
string getDirectory(string filename);

int mkdir_r(const char *dir, const mode_t mode);
int removeFolder(const char *folderPath, const char *logTag);

string toLower(string in);
string toUpper(string in);

float temperatureRead();

time_t addDays(time_t startTime, int days);

void mem_copy(uint8_t *_target, uint8_t *_source, int _size);
void mem_copy32(void *_target, const void *_source, size_t size);
float fast_atan2(float y, float x);

double pow10_double(int pot); // schneller als pow(10, pot)
uint32_t pow10_int(int pot);  // schneller als pow(10, pot)

std::vector<string> HelperZerlegeZeile(std::string input, std::string _delimiter);
std::vector<std::string> ZerlegeZeile(std::string input, std::string delimiter = " =, \t");

///////////////////////////
size_t getInternalESPHeapSize();
size_t getESPHeapSize();
string getESPHeapInfo();

/////////////////////////////
string getSDCardPartitionSize();
string getSDCardFreePartitionSpace();
string getSDCardPartitionAllocationSize();

void SaveSDCardInfo(sdmmc_card_t *card);
string SDCardParseManufacturerIDs(int);
string getSDCardManufacturer();
string getSDCardName();
string getSDCardCapacity();
string getSDCardSectorSize();

string getMac(void);

void setSystemStatusFlag(SystemStatusFlag_t flag);
void clearSystemStatusFlag(SystemStatusFlag_t flag);
int getSystemStatus(void);
bool isSetSystemStatusFlag(SystemStatusFlag_t flag);

time_t getUpTime(void);
string getResetReason(void);
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
