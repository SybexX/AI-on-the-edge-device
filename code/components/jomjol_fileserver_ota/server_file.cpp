/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "server_file.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include <esp_err.h>
#include <esp_log.h>

#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <esp_http_server.h>

#include <iostream>
#include <sys/types.h>
#include <dirent.h>

#include "defines.h"
#include "Helper.h"

#include "ClassLogFile.h"
#include "MainFlowControl.h"

#include "server_main.h"
#include "server_help.h"
#include "md5.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#endif // ENABLE_MQTT

#include "server_GpioPin.h"
#include "server_GpioHandler.h"

#include "minizip.h"
#include "basic_auth.h"

static const char *TAG = "FILE";

rest_server_context_t *rest_context = NULL;
string SUFFIX_TEMP = "_tmp";

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath, const char *uripath, bool readonly)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    char dirpath_corrected[FILE_PATH_MAX];
    strcpy(dirpath_corrected, dirpath);

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;

    if ((strlen(dirpath_corrected) - 1) > strlen(rest_context->base_path))
    {
        // if dirpath is not mountpoint, the last "\" needs to be removed
        dirpath_corrected[strlen(dirpath_corrected) - 1] = '\0';
    }

    DIR *pdir = opendir(dirpath_corrected);

    const size_t dirpath_len = strlen(dirpath);
    ESP_LOGD(TAG, "Dirpath: <%s>, Pathlength: %d", dirpath, dirpath_len);

    // Retrieve the base path of file storage to construct the full path
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    ESP_LOGD(TAG, "entrypath: <%s>", entrypath);

    if (!pdir)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + std::string(dirpath) + "!");
        // Respond with 404 Not Found
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Send HTML file header
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html lang=\"en\" xml:lang=\"en\"><head>");
    httpd_resp_sendstr_chunk(req, "<link href=\"/file_server.css\" rel=\"stylesheet\">");
    httpd_resp_sendstr_chunk(req, "<link href=\"/firework.css\" rel=\"stylesheet\">");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/jquery-3.6.0.min.js\"></script>");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/firework.js\"></script></head>");

    httpd_resp_sendstr_chunk(req, "<body>");

    httpd_resp_sendstr_chunk(req, "<table class=\"fixed\" border=\"0\" width=100% style=\"font-family: arial\">");
    httpd_resp_sendstr_chunk(req, "<tr><td style=\"vertical-align: top;width: 300px;\"><h2>Fileserver</h2></td>"
                                  "<td rowspan=\"2\"><table border=\"0\" style=\"width:100%\"><tr><td style=\"width:80px\">"
                                  "<label for=\"newfile\">Source</label></td><td colspan=\"2\">"
                                  "<input id=\"newfile\" type=\"file\" onchange=\"setpath()\" style=\"width:100%;\"></td></tr>"
                                  "<tr><td><label for=\"filepath\">Destination</label></td><td>"
                                  "<input id=\"filepath\" type=\"text\" style=\"width:94%;\"></td><td>"
                                  "<button id=\"upload\" type=\"button\" class=\"button\" onclick=\"upload()\">Upload</button></td></tr>"
                                  "</table></td></tr><tr></tr><tr><td colspan=\"2\">"
                                  "<button style=\"font-size:16px; padding: 5px 10px\" id=\"dirup\" type=\"button\" onclick=\"dirup()\""
                                  "disabled>&#129145; Directory up</button><span style=\"padding-left:15px\" id=\"currentpath\">"
                                  "</span></td></tr>");
    httpd_resp_sendstr_chunk(req, "</table>");

    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/file_server.js\"></script>");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\">initFileServer();</script>");

    std::string temp_dirpath = std::string(dirpath);
    temp_dirpath = temp_dirpath.substr(7, temp_dirpath.length() - 7);
    temp_dirpath = "/delete" + temp_dirpath + "?task=deldircontent";

    // Send file-list table definition and column labels
    httpd_resp_sendstr_chunk(req, "<table id=\"files_table\">"
                                  "<col width=\"800px\"><col width=\"300px\"><col width=\"300px\"><col width=\"100px\">"
                                  "<thead><tr><th>Name</th><th>Type</th><th>Size</th>");

    if (!readonly)
    {
        httpd_resp_sendstr_chunk(req, "<th><form method=\"post\" action=\"");
        httpd_resp_sendstr_chunk(req, temp_dirpath.c_str());
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">DELETE ALL!</button></form></th></tr>");
    }

    httpd_resp_sendstr_chunk(req, "</thead><tbody>\n");

    // Iterate over all files / folders and fetch their names and sizes
    while ((entry = readdir(pdir)) != NULL)
    {
        // wlan.ini soll nicht angezeigt werden!
        if ((strcmp("wlan.ini", entry->d_name) != 0) && (strcmp("network.ini", entry->d_name) != 0))
        {
            entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

            strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
            ESP_LOGD(TAG, "Entrypath: %s", entrypath);

            if (stat(entrypath, &entry_stat) == -1)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat " + std::string(entrytype) + ": " + std::string(entry->d_name));
                continue;
            }

            if (entry->d_type == DT_DIR)
            {
                strcpy(entrysize, "-\0");
            }
            else
            {
                if (entry_stat.st_size >= 1024)
                {
                    sprintf(entrysize, "%ld KiB", entry_stat.st_size / 1024); // kBytes
                }
                else
                {
                    sprintf(entrysize, "%ld B", entry_stat.st_size); // Bytes
                }
            }

            ESP_LOGD(TAG, "Found %s: %s (%s bytes)", entrytype, entry->d_name, entrysize);

            // Send chunk of HTML file containing table entries with file name and size
            httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
            httpd_resp_sendstr_chunk(req, "/fileserver");
            httpd_resp_sendstr_chunk(req, uripath);
            httpd_resp_sendstr_chunk(req, entry->d_name);

            if (entry->d_type == DT_DIR)
            {
                httpd_resp_sendstr_chunk(req, "/");
            }

            httpd_resp_sendstr_chunk(req, "\">");
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "</a></td><td>");
            httpd_resp_sendstr_chunk(req, entrytype);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);

            if (!readonly)
            {
                httpd_resp_sendstr_chunk(req, "</td><td>");
                httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
                httpd_resp_sendstr_chunk(req, uripath);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
            }

            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        }
    }

    closedir(pdir);

    // Finish the file list table
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    // Send remaining chunk of HTML file to complete it
    httpd_resp_sendstr_chunk(req, "</body></html>");

    // Send empty chunk to signal HTTP response completion
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static esp_err_t send_data_file_handler(httpd_req_t *req, bool send_full_file, bool is_logfile)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "send_data_file_handler");
    ESP_LOGD(TAG, "uri: %s", req->uri);

    std::string currentfilename = "";

    if (is_logfile)
    {
        currentfilename = LogFile.GetCurrentFileName();
        // Since the log file is still could open for writing, we need to close it first
        LogFile.CloseLogFileAppendHandle();
    }
    else
    {
        currentfilename = LogFile.GetCurrentFileNameData();
    }
    ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, currentfilename.c_str(), currentfilename.c_str());

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    FILE *pFile;
    if ((pFile = fopen(currentfilename.c_str(), "r")) == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + currentfilename + "!");
        // Respond with 404 Error
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    set_content_type_from_file(req, currentfilename.c_str());

    if (!send_full_file)
    {
        // Send only last part of file
        ESP_LOGD(TAG, "Sending last %d bytes of the actual file!", LOGFILE_LAST_PART_BYTES);

        if (fseek(pFile, 0, SEEK_END))
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to get to end of file!");
            return ESP_FAIL;
        }
        else
        {
            long pos = ftell(pFile); // Number of bytes in the file
            ESP_LOGD(TAG, "File contains %ld bytes", pos);

            if (fseek(pFile, pos - std::min((long)LOGFILE_LAST_PART_BYTES, pos), SEEK_SET))
            {
                // Go LOGFILE_LAST_PART_BYTES bytes back from EOF
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to go back " + std::to_string(std::min((long)LOGFILE_LAST_PART_BYTES, pos)) + " bytes within the file!");
                return ESP_FAIL;
            }
        }

        // Find end of line
        while (1)
        {
            if (fgetc(pFile) == '\n')
            {
                break;
            }
        }
    }

    // Retrieve the pointer to scratch buffer for temporary storage
    char *chunk = ((rest_server_context_t *)req->user_ctx)->scratch;
    size_t chunksize = 0;

    do
    {
        // Read file in chunks into the scratch buffer
        chunksize = fread(chunk, 1, SERVER_FILE_SCRATCH_BUFSIZE, pFile);

        // Send the buffer contents as HTTP response chunk
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {
            fclose(pFile);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File " + currentfilename + " sending failed!");

            // Abort sending file
            httpd_resp_sendstr_chunk(req, NULL);

            // Respond with 500 Internal Server Error
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }
    } while (chunksize != 0);

    // Close file after sending complete
    fclose(pFile);
    ESP_LOGD(TAG, "File sending complete");

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t logfileact_get_full_handler(httpd_req_t *req)
{
    return send_data_file_handler(req, true, true);
}

static esp_err_t logfileact_get_last_part_handler(httpd_req_t *req)
{
    return send_data_file_handler(req, false, true);
}

static esp_err_t datafileact_get_full_handler(httpd_req_t *req)
{
    return send_data_file_handler(req, true, false);
}

static esp_err_t datafileact_get_last_part_handler(httpd_req_t *req)
{
    return send_data_file_handler(req, false, false);
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/fileserver") - 1, sizeof(filepath));

    if (!filename)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Filename is too long");
        /* Respond with 414 Error */
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/')
    {
        bool readonly = false;
        size_t buf_len = httpd_req_get_url_query_len(req) + 1;
        if (buf_len > 1)
        {
            char buf[buf_len];
            if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
            {
                char param[32];
                /* Get value of expected key from query string */
                if (httpd_query_key_value(buf, "readonly", param, sizeof(param)) == ESP_OK)
                {
                    readonly = (strcmp(param, "true") == 0);
                }
            }
        }

        return http_resp_dir_html(req, filepath, filename, readonly);
    }

    std::string testwlan = toUpper(std::string(filename));

    if ((stat(filepath, &file_stat) == -1) || (testwlan.compare("/WLAN.INI") == 0) || (testwlan.compare("/NETWORK.INI") == 0))
    {
        // wlan.ini soll nicht angezeigt werden!
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat file: " + std::string(filepath) + "!");
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    FILE *pFile = fopen(filepath, "r");
    if (!pFile)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + std::string(filepath) + "!");
        /* Respond with 404 Error */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((rest_server_context_t *)req->user_ctx)->scratch;
    size_t chunksize = 0;

    do
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SERVER_FILE_SCRATCH_BUFSIZE, pFile);

        /* Send buffer contents as HTTP chunk. If empty this functions as a
         * last-chunk message, signaling end-of-response, to the HTTP client.
         * See RFC 2616, section 3.6.1 for details on Chunked Transfer Encoding. */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {
            fclose(pFile);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File " + std::string(filepath) + " sending failed!");

            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);

            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file!");

            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(pFile);

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename)
    {
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/')
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Invalid filename: " + string(filename));
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File already exists: " + string(filepath));
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File too large: " + to_string(req->content_len) + " bytes");
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size must be less than " MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    FILE *pFile = fopen(filepath, "w");
    if (!pFile)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create file: " + string(filepath));
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((rest_server_context_t *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0)
    {
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, chunk, MIN(remaining, SERVER_FILE_SCRATCH_BUFSIZE))) <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(pFile);
            unlink(filepath);

            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(chunk, 1, received, pFile)))
        {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(pFile);
            unlink(filepath);

            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(pFile);

    string s = req->uri;
    if (isInString(s, "?md5"))
    {
        pFile = fopen(filepath, "r");
        if (!pFile)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to open file for reading: " + string(filepath));
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file for reading");
            return ESP_FAIL;
        }

        uint8_t result[16];
        string md5hex = "";
        string response = "{\"md5\":";
        char hex[3];

        md5File(pFile, result);
        fclose(pFile);

        for (int i = 0; i < sizeof(result); i++)
        {
            snprintf(hex, sizeof(hex), "%02x", result[i]);
            md5hex.append(hex);
        }

        response.append("\"" + md5hex + "\"");
        response.append("}");

        httpd_resp_sendstr(req, response.c_str());
    }
    else
    {
        // Return file server page
        std::string directory = std::string(filepath);
        size_t temp_directory = directory.find("/");
        size_t found = temp_directory;
        while (temp_directory != std::string::npos)
        {
            temp_directory = directory.find("/", found + 1);
            if (temp_directory != std::string::npos)
            {
                found = temp_directory;
            }
        }

        int start_fn = strlen(((rest_server_context_t *)req->user_ctx)->base_path);

        directory = directory.substr(start_fn, found - start_fn + 1);
        directory = "/fileserver" + directory;

        /* Redirect onto root to see the updated file list */
        if (strcmp(filename, "/config/config.ini") == 0 ||
            strcmp(filename, "/config/ref0.jpg") == 0 ||
            strcmp(filename, "/config/ref0_org.jpg") == 0 ||
            strcmp(filename, "/config/ref1.jpg") == 0 ||
            strcmp(filename, "/config/ref1_org.jpg") == 0 ||
            strcmp(filename, "/config/reference.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/reference.jpg") == 0)
        {
            httpd_resp_set_status(req, HTTPD_200); // Avoid reloading of folder content
        }
        else
        {
            httpd_resp_set_status(req, "303 See Other"); // Reload folder content after upload
        }

        httpd_resp_set_hdr(req, "Location", directory.c_str());
        httpd_resp_sendstr(req, "File uploaded successfully");
    }

    return ESP_OK;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    char _query[200];
    char _valuechar[30];
    std::string fn = "/sdcard/firmware/";
    std::string _task;
    std::string directory;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
    {
        if (httpd_query_key_value(_query, "task", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            _task = std::string(_valuechar);
        }
    }

    if (_task.compare("deldircontent") == 0)
    {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename)
        {
            /* Respond with 414 Error */
            httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
            return ESP_FAIL;
        }

        std::string temp_filename = std::string(filename);
        temp_filename = temp_filename.substr(0, temp_filename.length() - 1);
        directory = "/fileserver" + temp_filename + "/";
        temp_filename = "/sdcard" + temp_filename;

        delete_all_in_directory(temp_filename);
    }
    else
    {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename)
        {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
            return ESP_FAIL;
        }

        /* Filename cannot have a trailing '/' */
        if (filename[strlen(filename) - 1] == '/')
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Invalid filename: " + string(filename));
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        if ((strcmp(filename, "wlan.ini") == 0) || (strcmp(filename, "network.ini") == 0))
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to delete protected file : " + string(filename));
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Not allowed to delete wlan.ini");
            return ESP_FAIL;
        }

        if (stat(filepath, &file_stat) == -1)
        {
            // File does not exist
            /* This is ok, we would delete it anyway */
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "File does not exist: " + string(filename));
        }

        // Delete file or folder
        if (removeFolder(filepath, TAG) != -1)
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Folder deleted: " + std::string(filename));
        }
        else
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "File deleted: " + std::string(filename));
            unlink(filepath);
        }

        directory = std::string(filepath);
        size_t temp_size = directory.find("/");
        size_t found = temp_size;
        while (temp_size != std::string::npos)
        {
            temp_size = directory.find("/", found + 1);
            if (temp_size != std::string::npos)
            {
                found = temp_size;
            }
        }

        int start_fn = strlen(((rest_server_context_t *)req->user_ctx)->base_path);

        directory = directory.substr(start_fn, found - start_fn + 1);
        directory = "/fileserver" + directory;

        /* Redirect onto root to see the updated file list */
        if (strcmp(filename, "/config/config.ini") == 0 ||
            strcmp(filename, "/config/ref0.jpg") == 0 ||
            strcmp(filename, "/config/ref0_org.jpg") == 0 ||
            strcmp(filename, "/config/ref1.jpg") == 0 ||
            strcmp(filename, "/config/ref1_org.jpg") == 0 ||
            strcmp(filename, "/config/reference.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/reference.jpg") == 0)
        {
            httpd_resp_set_status(req, HTTPD_200); // Avoid reloading of folder content
        }
        else
        {
            httpd_resp_set_status(req, "303 See Other"); // Reload folder content after upload
        }
    }

    httpd_resp_set_hdr(req, "Location", directory.c_str());
    httpd_resp_sendstr(req, "File successfully deleted");

    return ESP_OK;
}

esp_err_t get_number_list_handler(httpd_req_t *req)
{
    std::string ret = flowctrl.getNumbersName();

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    httpd_resp_sendstr_chunk(req, ret.c_str());
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

esp_err_t get_data_list_handler(httpd_req_t *req)
{
    struct dirent *entry;
    std::string _filename, _fileext;
    size_t pos = 0;

    const char verz_name[] = "/sdcard/log/data";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    DIR *pdir = opendir(verz_name);

    while ((entry = readdir(pdir)) != NULL)
    {
        _filename = std::string(entry->d_name);

        // ignore all files with starting dot (hidden files)
        if (_filename.rfind(".", 0) == 0)
        {
            continue;
        }

        _fileext = _filename;
        pos = _fileext.find_last_of(".");

        if (pos != std::string::npos)
        {
            _fileext = _fileext.erase(0, pos + 1);
        }

        if (_fileext == "csv")
        {
            _filename = _filename + "\t";
            httpd_resp_sendstr_chunk(req, _filename.c_str());
        }
    }

    closedir(pdir);
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

esp_err_t get_tflite_list_handler(httpd_req_t *req)
{
    struct dirent *entry;
    std::string _filename, _fileext;
    size_t pos = 0;

    const char verz_name[] = "/sdcard/config";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    DIR *pdir = opendir(verz_name);

    while ((entry = readdir(pdir)) != NULL)
    {
        _filename = std::string(entry->d_name);

        // ignore all files with starting dot (hidden files)
        if (_filename.rfind(".", 0) == 0)
        {
            continue;
        }

        _fileext = _filename;
        pos = _fileext.find_last_of(".");

        if (pos != std::string::npos)
        {
            _fileext = _fileext.erase(0, pos + 1);
        }

        if ((_fileext == "tfl") || (_fileext == "tflite"))
        {
            _filename = "/config/" + _filename + "\t";
            httpd_resp_sendstr_chunk(req, _filename.c_str());
        }
    }

    closedir(pdir);
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

esp_err_t get_config_list_handler(httpd_req_t *req)
{
    struct dirent *entry;
    std::string _filename, _fileext;
    size_t pos = 0;

    const char verz_name[] = "/sdcard/config";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);

    while ((entry = readdir(dir)) != NULL)
    {
        _filename = std::string(entry->d_name);

        // ignore all files with starting dot (hidden files)
        if (_filename.rfind(".", 0) == 0)
        {
            continue;
        }

        _fileext = _filename;
        pos = _fileext.find_last_of(".");

        if (pos != std::string::npos)
        {
            _fileext = _fileext.erase(0, pos + 1);
        }

        if ((isInString(_filename, "config")) && (_fileext == "ini"))
        {
            _filename = "/config/" + _filename + "\t";
            httpd_resp_sendstr_chunk(req, _filename.c_str());
        }
    }

    closedir(dir);
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

std::string unzip_ota_file(std::string _in_zip_file, std::string _html_tmp, std::string _html_final, std::string _target_bin, std::string _main, bool _initial_setup)
{
    int i, sort_iter;
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *p;
    char archive_filename[64];
    std::string filename_temp, filename_ret = "";
    std::string directory = "";

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    mzip_bool status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);
    if (!status)
    {
        return filename_ret;
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);

    sort_iter = 0;
    {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
        if (!status)
        {
            return filename_ret;
        }

        for (i = 0; i < numberoffiles; i++)
        {
            mzip_zip_archive_file_stat file_stat;
            mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
            sprintf(archive_filename, file_stat.m_filename);

            if (!file_stat.m_is_directory)
            {
                // Try to extract all the files to the heap.
                p = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
                if (!p)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_extract_file_to_heap() failed on file " + string(archive_filename));
                    mzip_zip_reader_end(&zip_archive);
                    return filename_ret;
                }

                // Save to File.
                filename_temp = std::string(archive_filename);

                if (toUpper(filename_temp) == "FIRMWARE.BIN")
                {
                    filename_temp = _target_bin + filename_temp;
                    filename_ret = filename_temp;
                }
                else
                {
                    std::string _dir = getDirectory(filename_temp);
                    if ((_dir == "config-initial") && !_initial_setup)
                    {
                        continue;
                    }
                    else
                    {
                        _dir = "config";
                        std::string _s1 = "config-initial";
                        FindReplace(filename_temp, _s1, _dir);
                    }

                    if (_dir.length() > 0)
                    {
                        filename_temp = _main + filename_temp;
                    }
                    else
                    {
                        filename_temp = _html_tmp + filename_temp;
                    }
                }

                // files in the html folder shall be redirected to the temporary html folder
                if (filename_temp.find(_html_final) == 0)
                {
                    FindReplace(filename_temp, _html_final, _html_tmp);
                }

                string _filename_temp = filename_temp + SUFFIX_TEMP;
                std::string folder = _filename_temp.substr(0, _filename_temp.find_last_of('/'));
                MakeDir(folder);

                // extrahieren in zwischendatei
                DeleteFile(_filename_temp);

                FILE *pFile = fopen(_filename_temp.c_str(), "wb");
                uint writtenbytes = fwrite(p, 1, (uint)uncomp_size, pFile);
                fclose(pFile);

                bool isokay = true;

                if (writtenbytes == (uint)uncomp_size)
                {
                    isokay = true;
                }
                else
                {
                    isokay = false;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in writting extracted file (function fwrite) extracted file \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                }

                DeleteFile(filename_temp);
                if (!isokay)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in fwrite \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                }

                isokay = isokay && RenameFile(_filename_temp, filename_temp);
                if (!isokay)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in Rename \"" + _filename_temp + "\" to \"" + filename_temp);
                }

                if (isokay)
                {
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully extracted file \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                }
                else
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in extracting file \"" + string(archive_filename) + "\", size " + to_string(uncomp_size));
                    filename_ret = "ERROR";
                }
                mzip_free(p);
            }
        }

        // Close the archive, freeing any resources it was using
        mzip_zip_reader_end(&zip_archive);
    }

    return filename_ret;
}

void unzip_file(std::string _in_zip_file, std::string _target_directory)
{
    int i, sort_iter;
    mzip_bool status;
    size_t uncomp_size;
    mzip_zip_archive zip_archive;
    void *p;
    char archive_filename[64];

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);
    if (!status)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
        return;
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mzip_zip_reader_get_num_files(&zip_archive);
    for (sort_iter = 0; sort_iter < 2; sort_iter++)
    {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mzip_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZIP_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
        if (!status)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_init_file() failed!");
            return;
        }

        for (i = 0; i < numberoffiles; i++)
        {
            mzip_zip_archive_file_stat file_stat;
            mzip_zip_reader_file_stat(&zip_archive, i, &file_stat);
            sprintf(archive_filename, file_stat.m_filename);

            // Try to extract all the files to the heap.
            p = mzip_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
            if (!p)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mzip_zip_reader_extract_file_to_heap() failed!");
                mzip_zip_reader_end(&zip_archive);
                return;
            }

            // Save to File.
            std::string filename_temp = std::string(archive_filename);
            filename_temp = _target_directory + filename_temp;
            FILE *pFile = fopen(filename_temp.c_str(), "wb");
            fwrite(p, 1, (uint)uncomp_size, pFile);
            fclose(pFile);

            mzip_free(p);
        }

        // Close the archive, freeing any resources it was using
        mzip_zip_reader_end(&zip_archive);
    }
}

esp_err_t register_server_file_uri(const char *base_path, httpd_handle_t my_server)
{
    if (!base_path)
    {
        ESP_LOGE(TAG, "wrong base path!");
        return ESP_FAIL;
    }

    rest_context = (rest_server_context_t *)calloc(1, sizeof(rest_server_context_t));
    if (!rest_context)
    {
        ESP_LOGE(TAG, "No memory for rest context!");
        free(rest_context);
        return ESP_FAIL;
    }
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    esp_err_t ret = ESP_OK;

    httpd_uri_t file_download = {
        .uri = "/fileserver*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(download_get_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &file_download);

    httpd_uri_t datafile_last_part_handle = {
        .uri = "/data", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(datafileact_get_last_part_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &datafile_last_part_handle);

    httpd_uri_t datafileact = {
        .uri = "/datafileact", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(datafileact_get_full_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &datafileact);

    httpd_uri_t logfile_last_part = {
        .uri = "/log", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(logfileact_get_last_part_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &logfile_last_part);

    httpd_uri_t logfileact = {
        .uri = "/logfileact", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(logfileact_get_full_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &logfileact);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri = "/upload/*", // Match all URIs of type /upload/path/to/file
        .method = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(upload_post_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri = "/delete/*", // Match all URIs of type /delete/path/to/file
        .method = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(delete_post_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &file_delete);

    return ret;
}
