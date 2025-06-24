FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "../../include" "../esp-protocols/components/mdns"
                    REQUIRES esp_wifi nvs_flash jomjol_helper jomjol_mqtt wpa_supplicant)


