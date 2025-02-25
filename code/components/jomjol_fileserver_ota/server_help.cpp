#include "server_help.h"

#include "minizip.h"

static const char *TAG = "SERVER HELP";

char scratch[SERVER_HELPER_SCRATCH_BUFSIZE];

bool endsWith(std::string const &str, std::string const &suffix)
{
    if (str.length() < suffix.length())
    {
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
    if (stat(_filename_temp.c_str(), &file_stat) == 0)
    {
        filename = _filename_temp;

        ESP_LOGD(TAG, "new filename: %s", filename.c_str());
        _gz_file_exists = true;
    }

    FILE *pfile;
    if ((pfile = fopen(filename.c_str(), "r")) == NULL)
    {
        ESP_LOGE(TAG, "Failed to read file: %s", filename.c_str());

        // Respond with 404 Error
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());

        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sending file: %s ...", filename.c_str());

    // For all files with the following file extention tell the webbrowser to cache them for 24h
    if (endsWith(filename, ".html") ||
        endsWith(filename, ".htm") ||
        endsWith(filename, ".xml") ||
        endsWith(filename, ".css") ||
        endsWith(filename, ".js") ||
        endsWith(filename, ".map") ||
        endsWith(filename, ".jpg") ||
        endsWith(filename, ".jpeg") ||
        endsWith(filename, ".ico") ||
        endsWith(filename, ".png") ||
        endsWith(filename, ".gif") ||
        // endsWith(filename, ".zip") ||
        endsWith(filename, ".gz"))
    {
        if (filename == "/sdcard/html/setup.html")
        {
            httpd_resp_set_hdr(req, "Clear-Site-Data", "\"*\"");
            set_content_type_from_file(req, filename.c_str());
        }
        else if (_gz_file_exists)
        {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=43200");
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            set_content_type_from_file(req, _filename_old.c_str());
        }
        else
        {
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=43200");
            set_content_type_from_file(req, filename.c_str());
        }
    }
    else
    {
        set_content_type_from_file(req, filename.c_str());
    }

    // Retrieve the pointer to scratch buffer for temporary storage
    char *chunk = scratch;
    size_t chunksize;

    do
    {
        // Read file in chunks into the scratch buffer
        chunksize = fread(chunk, 1, SERVER_HELPER_SCRATCH_BUFSIZE, pfile);

        // Send the buffer contents as HTTP response chunk
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {
            fclose(pfile);
            ESP_LOGE(TAG, "File sending failed!");

            // Abort sending file
            httpd_resp_sendstr_chunk(req, NULL);

            // Respond with 500 Internal Server Error
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");

            return ESP_FAIL;
        }

        // Keep looping till the whole file is sent
    } while (chunksize != 0);

    // Close file after sending complete
    fclose(pfile);
    ESP_LOGD(TAG, "File sending complete");

    return ESP_OK;
}

// Copies the full path into destination buffer and returns
// pointer to path (skipping the preceding base path)
const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest)
    {
        pathlen = MIN(pathlen, quest - uri);
    }

    const char *hash = strchr(uri, '#');
    if (hash)
    {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        // Full path string won't fit into destination buffer
        return NULL;
    }

    // Construct full path (base + path)
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    // Return pointer to path, skipping the base
    return dest + base_pathlen;
}

// Set HTTP response content type according to file extension
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf"))
    {
        return httpd_resp_set_type(req, "application/x-pdf");
    }
    else if (IS_FILE_EXT(filename, ".htm"))
    {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".html"))
    {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg"))
    {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".jpg"))
    {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".gif"))
    {
        return httpd_resp_set_type(req, "image/gif");
    }
    else if (IS_FILE_EXT(filename, ".png"))
    {
        return httpd_resp_set_type(req, "image/png");
    }
    else if (IS_FILE_EXT(filename, ".ico"))
    {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".js"))
    {
        return httpd_resp_set_type(req, "application/javascript");
    }
    else if (IS_FILE_EXT(filename, ".css"))
    {
        return httpd_resp_set_type(req, "text/css");
    }
    else if (IS_FILE_EXT(filename, ".xml"))
    {
        return httpd_resp_set_type(req, "text/xml");
    }
    else if (IS_FILE_EXT(filename, ".zip"))
    {
        return httpd_resp_set_type(req, "application/x-zip");
    }
    else if (IS_FILE_EXT(filename, ".gz"))
    {
        return httpd_resp_set_type(req, "application/x-gzip");
    }

    // This is a limited set only
    // For any other type always set as plain text
    return httpd_resp_set_type(req, "text/plain");
}

void delete_all_in_directory(std::string _directory)
{
    struct dirent *entry;
    DIR *pdir = opendir(_directory.c_str());
    std::string filename;

    if (!pdir)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + _directory);
        return;
    }

    // Iterate over all files / folders and fetch their names and sizes
    while ((entry = readdir(pdir)) != NULL)
    {
        if (strcmp("wlan.ini", entry->d_name) != 0)
        {
            // auf wlan.ini soll nicht zugegriffen werden !!!
            filename = _directory + "/" + std::string(entry->d_name);
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleting file: " + filename);
            // Delete file
            unlink(filename.c_str());
        }
    }

    closedir(pdir);
}

void delete_all_file_in_directory(std::string _directory)
{
    struct dirent *entry;
    DIR *pdir = opendir(_directory.c_str());
    std::string filename;

    if (!pdir)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + _directory);
        return;
    }

    // Iterate over all files / folders and fetch their names and sizes
    while ((entry = readdir(pdir)) != NULL)
    {
        if (!(entry->d_type == DT_DIR))
        {
            if (strcmp("wlan.ini", entry->d_name) != 0)
            {
                // auf wlan.ini soll nicht zugegriffen werden !!!
                filename = _directory + "/" + std::string(entry->d_name);
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleting file: " + filename);
                // Delete file
                unlink(filename.c_str());
            }
        };
    }

    closedir(pdir);
}

std::string unzip_ota(std::string _in_zip_file, std::string _target_directory)
{
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *pzip;
    char archive_filename[256];
    std::string ret = "";

    ESP_LOGD(TAG, "minizip.c version: %s", MZIP_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s", _in_zip_file.c_str());

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    mzip_bool status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);

    if (!status)
    {
        ESP_LOGD(TAG, "mzip_zip_reader_init_file() failed!");
        return "ERROR";
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Files to be extracted: " + std::to_string(numberoffiles));
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Files are extracted, please wait.......");

    int sort_iter = 0;
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);

    if (!status)
    {
        ESP_LOGD(TAG, "mzip_zip_reader_init_file() failed!");
        return "ERROR";
    }

    for (int i = 0; i < numberoffiles; i++)
    {
        mzip_zip_archive_file_stat file_stat;
        mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
        sprintf(archive_filename, file_stat.m_filename);

        // Copy the file if it is not a folder
        if (!file_stat.m_is_directory)
        {
            std::string filepath = "";

            // Try to extract all the files to the heap.
            pzip = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);

            if (!pzip)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_extract_file_to_heap() failed on file " + std::string(archive_filename));
                mzip_zip_reader_end(&zip_archive);
                return "ERROR";
            }

            // Create the right data path
            std::string archive_filename_zw = std::string(archive_filename);
            ESP_LOGD(TAG, "Rawfilename: %s", archive_filename_zw.c_str());

            if (toUpper(archive_filename_zw) == "FIRMWARE.BIN")
            {
                filepath = _target_directory + "firmware/" + archive_filename_zw;
                ret = filepath;
            }
            else if (toUpper(archive_filename_zw) == "BOOTLOADER.BIN" || toUpper(archive_filename_zw) == "PARTITIONS.BIN" || toUpper(archive_filename_zw) == "README.MD")
            {
                // are not required and should therefore not be copied
                continue;
            }
            else
            {
                filepath = _target_directory + archive_filename_zw;
            }

            // Create the temp path of the file
            std::string filepath_zw = filepath + "_0xge";
            ESP_LOGD(TAG, "File to extract: %s, Temp. Filename: %s", filepath.c_str(), filepath_zw.c_str());

            // Create the required folder if it is not yet available
            MakeDir(getDirectory(filepath_zw));

            // delete the temp file if there is still available
            DeleteFile(filepath_zw);

            // Create the temp file
            FILE *pfile = fopen(filepath_zw.c_str(), "wb");
            uint writtenbytes = fwrite(pzip, 1, (uint)uncomp_size, pfile);
            fclose(pfile);
            mzip_free(pzip);

            if (writtenbytes != (uint)uncomp_size)
            {
                // If the creation of the temp file has failed, delete the temp file
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in writting extracted file (function fwrite) extracted file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                DeleteFile(filepath_zw);
            }
            else
            {
                // If the temp file was written correctly, delete the old file
                DeleteFile(filepath);
                // DeleteFileNormalAndGzip(filepath); // Could give problems if both are needed

                // Rename the temp file correctly
                if (RenameFile(filepath_zw, filepath))
                {
                    // If everything is ok and there are still files, continue
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully extracted file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                }
                else
                {
                    // If the renaming fails, break off
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in extracting file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                    ret = "ERROR";
                    break;
                }
            }
        }
    }

    // Close the archive, freeing any resources it was using
    mzip_zip_reader_end(&zip_archive);

    ESP_LOGD(TAG, "Successfully extracted file");
    return ret;
}

std::string unzip_file(std::string _in_zip_file, std::string _target_directory)
{
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *pzip;
    char archive_filename[256];
    std::string ret = "OK";

    ESP_LOGD(TAG, "minizip.c version: %s", MZIP_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s", _in_zip_file.c_str());
    ESP_LOGD(TAG, "Target Dir: %s", _target_directory.c_str());

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    mzip_bool status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);

    if (!status)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
        return "ERROR";
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);

    for (int sort_iter = 0; sort_iter < 2; sort_iter++)
    {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);

        if (!status)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
            return "ERROR";
        }

        for (int i = 0; i < numberoffiles; i++)
        {
            mzip_zip_archive_file_stat file_stat;
            mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
            sprintf(archive_filename, file_stat.m_filename);

            // Try to extract all the files to the heap.
            pzip = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);

            if (!pzip)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_extract_file_to_heap() failed!");
                mzip_zip_reader_end(&zip_archive);
                return "ERROR";
            }

            // Create the right file path
            std::string archive_filename_zw = std::string(archive_filename);
            std::string filepath = _target_directory + archive_filename_zw;
            ESP_LOGD(TAG, "File to extract: %s", filepath.c_str());

            // Create the temp path of the file
            std::string filepath_zw = filepath + "_0xge";
            ESP_LOGD(TAG, "File to extract: %s, Temp. Filename: %s", filepath.c_str(), filepath_zw.c_str());

            // Create the required folder if it is not yet available
            MakeDir(getDirectory(filepath_zw));

            // delete the temp file if there is still available
            DeleteFile(filepath_zw);

            // Create the temp file
            FILE *pfile = fopen(filepath_zw.c_str(), "wb");
            uint writtenbytes = fwrite(pzip, 1, (uint)uncomp_size, pfile);
            fclose(pfile);
            mzip_free(pzip);

            if (writtenbytes != (uint)uncomp_size)
            {
                // If the creation of the temp file has failed, delete the temp file
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in writting extracted file (function fwrite) extracted file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                DeleteFile(filepath_zw);
            }
            else
            {
                // If the temp file was written correctly, delete the old file
                DeleteFile(filepath);
                // DeleteFileNormalAndGzip(filepath); // Could give problems if both are needed

                // Rename the temp file correctly
                if (RenameFile(filepath_zw, filepath))
                {
                    // If everything is ok and there are still files, continue
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully extracted file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                }
                else
                {
                    // If the renaming fails, breaks off
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in extracting file \"" + std::string(archive_filename) + "\", size " + std::to_string(uncomp_size));
                    ret = "ERROR";
                    break;
                }
            }
        }
    }

    // Close the archive, freeing any resources it was using
    mzip_zip_reader_end(&zip_archive);

    ESP_LOGD(TAG, "Successfully extracted file");
    return ret;
}
