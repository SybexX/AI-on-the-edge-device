#include "server_help.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <dirent.h>
#ifdef __cplusplus
}
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "ClassLogFile.h"

#include "minizip.h"
#include "Helper.h"
#include "../../include/defines.h"

static const char *TAG = "SERVER HELP";

string SUFFIX_ZW = "_tmp";
char scratch[SERVER_HELPER_SCRATCH_BUFSIZE];

bool endsWith(std::string const &str, std::string const &suffix)
{
    if (str.length() < suffix.length()) {
        return false;
    }

    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

esp_err_t send_file(httpd_req_t *req, std::string filename)
{
    std::string _filename_old = filename;
    struct stat file_stat;
    bool _gz_file_exists = false;

    ESP_LOGD(TAG, "old filename: %s", filename.c_str());
    std::string _filename_temp = std::string(filename) + ".gz";

    // Checks whether the file is available as .gz
    if (stat(_filename_temp.c_str(), &file_stat) == 0) {
        filename = _filename_temp;

        ESP_LOGD(TAG, "new filename: %s", filename.c_str());
        _gz_file_exists = true;
    }

    FILE *fd = fopen(filename.c_str(), "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read file: %s", filename.c_str());

        /* Respond with 404 Error */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sending file: %s ...", filename.c_str());

    /* For all files with the following file extention tell the webbrowser to cache them for 12h */
    if (endsWith(filename, ".html") || endsWith(filename, ".htm") || endsWith(filename, ".xml") || endsWith(filename, ".css") ||
        endsWith(filename, ".js") || endsWith(filename, ".map") || endsWith(filename, ".jpg") || endsWith(filename, ".jpeg") ||
        endsWith(filename, ".ico") || endsWith(filename, ".png") || endsWith(filename, ".gif") ||
        // endsWith(filename, ".zip") ||
        endsWith(filename, ".gz")) {
        if (filename == "/sdcard/html/setup.html") {
            httpd_resp_set_hdr(req, "Clear-Site-Data", "\"*\"");
            set_content_type_from_file(req, filename.c_str());
        }
        else if (_gz_file_exists) {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=43200");
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            set_content_type_from_file(req, _filename_old.c_str());
        }
        else {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=43200");
            set_content_type_from_file(req, filename.c_str());
        }
    }
    else {
        set_content_type_from_file(req, filename.c_str());
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = scratch;
    size_t chunksize;

    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SERVER_HELPER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            ESP_LOGE(TAG, "File sending failed!");

            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);

            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");

            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File sending complete");

    return ESP_OK;
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }

    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Set HTTP response content type according to file extension */
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/x-pdf");
    }
    else if (IS_FILE_EXT(filename, ".htm")) {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".jpg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".gif")) {
        return httpd_resp_set_type(req, "image/gif");
    }
    else if (IS_FILE_EXT(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    }
    else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "application/javascript");
    }
    else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    }
    else if (IS_FILE_EXT(filename, ".xml")) {
        return httpd_resp_set_type(req, "text/xml");
    }
    else if (IS_FILE_EXT(filename, ".zip")) {
        return httpd_resp_set_type(req, "application/x-zip");
    }
    else if (IS_FILE_EXT(filename, ".gz")) {
        return httpd_resp_set_type(req, "application/x-gzip");
    }

    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

void delete_all_in_directory(std::string _directory)
{
    struct dirent *entry;
    DIR *dir = opendir(_directory.c_str());
    std::string filename;

    if (!dir) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + _directory);
        return;
    }

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        if (!(entry->d_type == DT_DIR)) {
            if (strcmp("wlan.ini", entry->d_name) != 0) {
                // auf wlan.ini soll nicht zugegriffen werden !!!
                filename = _directory + "/" + std::string(entry->d_name);
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleting file: " + filename);
                /* Delete file */
                unlink(filename.c_str());
            }
        };
    }
    closedir(dir);
}

std::string unzip_ota(std::string _in_zip_file, std::string _html_tmp, std::string _html_final, std::string _target_bin, std::string _main,
                      bool _initial_setup)
{
    int i, sort_iter;
    mzip_bool status;
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *p;
    char archive_filename[64];
    std::string zw, ret = "";
    std::string directory = "";

    ESP_LOGD(TAG, "miniz.c version: %s", MZIP_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s", _in_zip_file.c_str());

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);
    if (!status) {
        ESP_LOGD(TAG, "mzip_zip_reader_init_file() failed!");
        return ret;
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Files to be extracted: " + to_string(numberoffiles));

    sort_iter = 0;
    {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
        if (!status) {
            ESP_LOGD(TAG, "mzip_zip_reader_init_file() failed!");
            return ret;
        }

        for (i = 0; i < numberoffiles; i++) {
            mzip_zip_archive_file_stat file_stat;
            mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
            sprintf(archive_filename, file_stat.m_filename);

            if (!file_stat.m_is_directory) {
                // Try to extract all the files to the heap.
                p = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
                if (!p) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                                        "mzip_zip_reader_extract_file_to_heap() failed on file " + string(archive_filename));
                    mzip_zip_reader_end(&zip_archive);
                    return ret;
                }

                // Save to File.
                zw = std::string(archive_filename);
                ESP_LOGD(TAG, "Rohfilename: %s", zw.c_str());

                if (toUpper(zw) == "FIRMWARE.BIN") {
                    zw = _target_bin + zw;
                    ret = zw;
                }
                else {
                    std::string _dir = getDirectory(zw);
                    if ((_dir == "config-initial") && !_initial_setup) {
                        continue;
                    }
                    else {
                        _dir = "config";
                        std::string _s1 = "config-initial";
                        FindReplace(zw, _s1, _dir);
                    }

                    if (_dir.length() > 0) {
                        zw = _main + zw;
                    }
                    else {
                        zw = _html_tmp + zw;
                    }
                }

                // files in the html folder shall be redirected to the temporary html folder
                if (zw.find(_html_final) == 0) {
                    FindReplace(zw, _html_final, _html_tmp);
                }

                string filename_zw = zw + SUFFIX_ZW;

                ESP_LOGI(TAG, "File to extract: %s, Temp. Filename: %s", zw.c_str(), filename_zw.c_str());

                std::string folder = filename_zw.substr(0, filename_zw.find_last_of('/'));
                MakeDir(folder);

                // extrahieren in zwischendatei
                DeleteFile(filename_zw);

                FILE *fpTargetFile = fopen(filename_zw.c_str(), "wb");
                uint writtenbytes = fwrite(p, 1, (uint)uncomp_size, fpTargetFile);
                fclose(fpTargetFile);

                bool isokay = true;

                if (writtenbytes == (uint)uncomp_size) {
                    isokay = true;
                }
                else {
                    isokay = false;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                                        "ERROR in writting extracted file (function fwrite) extracted file \"" + string(archive_filename) +
                                            "\", size " + to_string(uncomp_size));
                }

                DeleteFile(zw);
                if (!isokay) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                                        "ERROR in fwrite \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                }

                isokay = isokay && RenameFile(filename_zw, zw);

                if (!isokay) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in Rename \"" + filename_zw + "\" to \"" + zw);
                }

                if (isokay) {
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG,
                                        "Successfully extracted file \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                }
                else {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                                        "ERROR in extracting file \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                    ret = "ERROR";
                }
                mzip_free(p);
            }
        }

        // Close the archive, freeing any resources it was using
        mzip_zip_reader_end(&zip_archive);
    }

    ESP_LOGD(TAG, "Success.");
    return ret;
}

void unzip_file(std::string _in_zip_file, std::string _target_directory)
{
    int i, sort_iter;
    mzip_bool status;
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *p;
    char archive_filename[64];
    std::string zw;

    ESP_LOGD(TAG, "miniz.c version: %s", MZIP_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s", _in_zip_file.c_str());
    ESP_LOGD(TAG, "Target Dir: %s", _target_directory.c_str());

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);
    if (!status) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
        return;
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);
    for (sort_iter = 0; sort_iter < 2; sort_iter++) {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
        if (!status) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
            return;
        }

        for (i = 0; i < numberoffiles; i++) {
            mzip_zip_archive_file_stat file_stat;
            mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
            sprintf(archive_filename, file_stat.m_filename);

            // Try to extract all the files to the heap.
            p = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
            if (!p) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_extract_file_to_heap() failed!");
                mzip_zip_reader_end(&zip_archive);
                return;
            }

            // Save to File.
            zw = std::string(archive_filename);
            zw = _target_directory + zw;
            ESP_LOGD(TAG, "File to extract: %s", zw.c_str());
            FILE *fpTargetFile = fopen(zw.c_str(), "wb");
            fwrite(p, 1, (uint)uncomp_size, fpTargetFile);
            fclose(fpTargetFile);

            ESP_LOGD(TAG, "Successfully extracted file \"%s\", size %u", archive_filename, (uint)uncomp_size);

            // We're done.
            mzip_free(p);
        }

        // Close the archive, freeing any resources it was using
        mzip_zip_reader_end(&zip_archive);
    }

    ESP_LOGD(TAG, "Success.");
}
