FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "../../include"
                    REQUIRES driver app_update vfs nvs_flash spiffs esp_http_server esp_http_client jomjol_tfliteclass jomjol_flowcontroll jomjol_helper jomjol_controlGPIO)


