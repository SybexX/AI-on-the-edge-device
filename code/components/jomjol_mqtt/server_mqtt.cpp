#ifdef ENABLE_MQTT
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#include "esp_log.h"
#include "ClassLogFile.h"
#include "connect_wlan.h"
#include "read_wlanini.h"
#include "server_mqtt.h"
#include "interface_mqtt.h"
#include "time_sntp.h"
#include "../../include/defines.h"
#include "basic_auth.h"



static const char *TAG = "MQTT SERVER";


extern const char* libfive_git_version(void);
extern const char* libfive_git_revision(void);
extern const char* libfive_git_branch(void);
extern std::string getFwVersion(void);

std::vector<NumberPost*>* NUMBERS;
bool HomeassistantDiscovery = false;
std::string meterType = "";
std::string valueUnit = "";
std::string timeUnit = "";
std::string rateUnit = "Unit/Minute";
float roundInterval; // Minutes
int keepAlive = 0; // Seconds
bool retainFlag;
static std::string maintopic, domoticzintopic;
bool sendingOf_DiscoveryAndStaticTopics_scheduled = true; // Set it to true to make sure it gets sent at least once after startup



void mqttServer_setParameter(std::vector<NumberPost*>* _NUMBERS, int _keepAlive, float _roundInterval) {
    NUMBERS = _NUMBERS;
    keepAlive = _keepAlive;
    roundInterval = _roundInterval; 
}

void mqttServer_setMeterType(std::string _meterType, std::string _valueUnit, std::string _timeUnit,std::string _rateUnit) {
    meterType = _meterType;
    valueUnit = _valueUnit;
    timeUnit = _timeUnit;
    rateUnit = _rateUnit;
}

/**
 * Takes any multi-level MQTT-topic and returns the last topic level as nodeId
 * see https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/ for details about MQTT topics
*/
std::string createNodeId(std::string &topic) {
    auto splitPos = topic.find_last_of('/');
    return (splitPos == std::string::npos) ? topic : topic.substr(splitPos + 1);
}

bool sendHomeAssistantDiscoveryTopic(std::string group, std::string field,
    std::string name, std::string icon, std::string unit, std::string deviceClass, std::string stateClass, std::string entityCategory,
    int qos) {
    std::string version = std::string(libfive_git_version());

    if (version == "") {
        version = std::string(libfive_git_branch()) + " (" + std::string(libfive_git_revision()) + ")";
    }
    
    std::string topicFull;
    std::string configTopic;
    std::string payload;

    configTopic = field;

    if (group != "" && (*NUMBERS).size() > 1) { // There is more than one meter, prepend the group so we can differentiate them
        configTopic = group + "_" + field;
        name = group + " " + name;
    }    

    /** 
     * homeassistant needs the MQTT discovery topic according to the following structure:
     *      <discovery_prefix>/<component>/[<node_id>/]<object_id>/config
     * if the main topic is embedded in a nested structure, we just use the last part as node_id 
     * This means a maintopic "home/test/watermeter" is transformed to the discovery topic "homeassistant/sensor/watermeter/..."
    */
    std::string node_id = createNodeId(maintopic);
    if (field == "problem") { // Special case: Binary sensor which is based on error topic
        topicFull = "homeassistant/binary_sensor/" + node_id + "/" + configTopic + "/config";
    }
    else if (field == "flowstart") { // Special case: Button
        topicFull = "homeassistant/button/" + node_id + "/" + configTopic + "/config";
    }
    else {
        topicFull = "homeassistant/sensor/" + node_id + "/" + configTopic + "/config";
    }

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    payload = string("{")  +
        "\"~\": \"" + maintopic + "\","  +
        "\"unique_id\": \"" + maintopic + "-" + configTopic + "\","  +
        "\"object_id\": \"" + maintopic + "_" + configTopic + "\","  + // This used to generate the Entity ID
        "\"name\": \"" + name + "\","  +
        "\"icon\": \"mdi:" + icon + "\",";        

    if (group != "") {
        if (field == "problem") { // Special case: Binary sensor which is based on error topic
            payload += "\"state_topic\": \"~/" + group + "/error\",";
            payload += "\"value_template\": \"{{ 'OFF' if 'no error' in value else 'ON'}}\",";
        }
        else {
            payload += "\"state_topic\": \"~/" + group + "/" + field + "\",";
        }
    }
    else {
        if (field == "problem") { // Special case: Binary sensor which is based on error topic
            payload += "\"state_topic\": \"~/error\",";
            payload += "\"value_template\": \"{{ 'OFF' if 'no error' in value else 'ON'}}\",";
        }
        else if (field == "flowstart") { // Special case: Button
            payload += "\"cmd_t\":\"~/ctrl/flow_start\","; // Add command topic
        }
        else {
            payload += "\"state_topic\": \"~/" + field + "\",";
        }
    }

    if (unit != "") {
        payload += "\"unit_of_meas\": \"" + unit + "\",";
    }

    if (deviceClass != "") {
        payload += "\"device_class\": \"" + deviceClass + "\",";
    }

    if (stateClass != "") {
        payload += "\"state_class\": \"" + stateClass + "\",";
    } 

    if (entityCategory != "") {
        payload += "\"entity_category\": \"" + entityCategory + "\",";
    } 

    payload += 
        "\"availability_topic\": \"~/" + std::string(LWT_TOPIC) + "\","  +
        "\"payload_available\": \"" + LWT_CONNECTED + "\","  +
        "\"payload_not_available\": \"" + LWT_DISCONNECTED + "\",";

    payload += string("\"device\": {")  +
        "\"identifiers\": [\"" + maintopic + "\"],"  +
        "\"name\": \"" + maintopic + "\","  +
        "\"model\": \"Meter Digitizer\","  +
        "\"manufacturer\": \"AI on the Edge Device\","  +
      "\"sw_version\": \"" + version + "\","  +
      "\"configuration_url\": \"http://" + *getIPAddress() + "\""  +
    "}"  +
    "}";

    return MQTTPublish(topicFull, payload, qos, true);
}

bool MQTThomeassistantDiscovery(int qos) {  
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to send Homeassistant Discovery Topics, we are not connected to the MQTT broker!");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing Homeassistant Discovery topics (Meter Type: '" + meterType + "', Value Unit: '" + valueUnit + "' , Rate Unit: '" + rateUnit + "') ...");

	int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    //                                                   Group | Field            | User Friendly Name | Icon                      | Unit | Device Class     | State Class  | Entity Category
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "uptime",          "Uptime",            "clock-time-eight-outline", "s",   "",                "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "MAC",             "MAC Address",       "network-outline",          "",    "",                "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "fwVersion",       "Firmware Version",  "application-outline",      "",    "",                "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "hostname",        "Hostname",          "network-outline",          "",    "",                "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "freeMem",         "Free Memory",       "memory",                   "B",   "",                "measurement", "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "wifiRSSI",        "Wi-Fi RSSI",        "wifi",                     "dBm", "signal_strength", "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "CPUtemp",         "CPU Temperature",   "thermometer",              "°C",  "temperature",     "measurement", "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "interval",        "Interval",          "clock-time-eight-outline", "min",  ""           ,    "measurement", "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "IP",              "IP",                "network-outline",           "",    "",               "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "status",          "Status",            "list-status",               "",    "",               "",            "diagnostic", qos);
    allSendsSuccessed |= sendHomeAssistantDiscoveryTopic("",     "flowstart",       "Manual Flow Start", "timer-play-outline",        "",    "",               "",            "",           qos);


    for (int i = 0; i < (*NUMBERS).size(); ++i) {
        std::string group = (*NUMBERS)[i]->name;
        if (group == "default") {
            group = "";
        }

        /* If "Allow neg. rate" is true, use "measurement" instead of "total_increasing" for the State Class, see https://github.com/jomjol/AI-on-the-edge-device/issues/3331 */
        std::string value_state_class = "total_increasing";
        if (meterType == "temperature") {
            value_state_class = "measurement";
        }
        else if ((*NUMBERS)[i]->AllowNegativeRates) {
            value_state_class = "total";
        }

        /* Energy meters need a different Device Class, see https://github.com/jomjol/AI-on-the-edge-device/issues/3333 */
        std::string rate_device_class = "volume_flow_rate";
        if (meterType == "energy") {
            rate_device_class = "power";
        }

    //                                                       Group   | Field                       | User Friendly Name                    | Icon                       | Unit                 | Device Class     | State Class       | Entity Category | QoS
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "value",                      "Value",                                "gauge",                     valueUnit,             meterType,         value_state_class,  "",               qos); // State Class = "total_increasing" if <NUMBERS>.AllowNegativeRates = false, "measurement" in case of a thermometer, else use "total".
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "raw",                        "Raw Value",                            "raw",                       valueUnit,             meterType,         value_state_class,  "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "error",                      "Error",                                "alert-circle-outline",      "",                    "",                "",                 "diagnostic",     qos);
        /* Not announcing "rate" as it is better to use rate_per_time_unit resp. rate_per_digitization_round */
     // allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "rate",                       "Rate (Unit/Minute)",                   "swap-vertical",             "",                    "",                "",                 "",               qos); // Legacy, always Unit per Minute
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "rate_per_time_unit",         "Rate (" + rateUnit + ")",              "swap-vertical",             rateUnit,              rate_device_class, "measurement",      "",               qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "rate_per_digitization_round","Change since last Digitization round", "arrow-expand-vertical",     valueUnit,             "",                "measurement",      "",               qos); // correctly the Unit is Unit/Interval!
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "timestamp",                  "Timestamp",                            "clock-time-eight-outline",  "",                    "timestamp",       "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "json",                       "JSON",                                 "code-json",                 "",                    "",                "",                 "diagnostic",     qos);
        allSendsSuccessed |= sendHomeAssistantDiscoveryTopic(group,   "problem",                    "Problem",                              "alert-outline",             "",                    "problem",         "",                 "",               qos); // Special binary sensor which is based on error topic
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published all Homeassistant Discovery MQTT topics");

    int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before Publishing Homeassistand Discovery Topics: " + 
            to_string(aFreeInternalHeapSizeBefore) + ", after: " + to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + to_string(aMinFreeInternalHeapSize));

    return allSendsSuccessed;
}

bool publishSystemData(int qos) {
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to send System Topics, we are not connected to the MQTT broker!");
        return false;
    }

    char tmp_char[50];

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing System MQTT topics...");

	int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    allSendsSuccessed |= MQTTPublish(maintopic + "/" + std::string(LWT_TOPIC), LWT_CONNECTED, qos, retainFlag); // Publish "connected" to maintopic/connection

    sprintf(tmp_char, "%ld", (long)getUpTime());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "uptime", std::string(tmp_char), qos, retainFlag);
    
    sprintf(tmp_char, "%lu", (long) getESPHeapSize());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "freeMem", std::string(tmp_char), qos, retainFlag);

    sprintf(tmp_char, "%d", get_WIFI_RSSI());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "wifiRSSI", std::string(tmp_char), qos, retainFlag);

    sprintf(tmp_char, "%d", (int)temperatureRead());
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "CPUtemp", std::string(tmp_char), qos, retainFlag);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published all System MQTT topics");

	int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before publishing System Topics: " + 
            to_string(aFreeInternalHeapSizeBefore) + ", after: " + to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + to_string(aMinFreeInternalHeapSize));

    return allSendsSuccessed;
}


bool publishStaticData(int qos) {
    bool allSendsSuccessed = false;

    if (!getMQTTisConnected()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unable to send Static Topics, we are not connected to the MQTT broker!");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing static MQTT topics...");

	int aFreeInternalHeapSizeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "fwVersion", getFwVersion().c_str(), qos, retainFlag);
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "MAC", getMac(), qos, retainFlag);
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "IP", *getIPAddress(), qos, retainFlag);
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "hostname", wlan_config.hostname, qos, retainFlag);

    std::stringstream stream;
    stream << std::fixed << std::setprecision(1) << roundInterval; // minutes
    allSendsSuccessed |= MQTTPublish(maintopic + "/" + "interval", stream.str(), qos, retainFlag);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully published all Static MQTT topics");

	int aFreeInternalHeapSizeAfter = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	int aMinFreeInternalHeapSize =  heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Int. Heap Usage before Publishing Static Topics: " + 
            to_string(aFreeInternalHeapSizeBefore) + ", after: " + to_string(aFreeInternalHeapSizeAfter) + ", delta: " + 
            to_string(aFreeInternalHeapSizeBefore - aFreeInternalHeapSizeAfter) + ", lowest free: " + to_string(aMinFreeInternalHeapSize));

    return allSendsSuccessed;
}


esp_err_t scheduleSendingDiscovery_and_static_Topics(httpd_req_t *req) {
    sendingOf_DiscoveryAndStaticTopics_scheduled = true;
    char msg[] = "MQTT Homeassistant Discovery and Static Topics scheduled";
    httpd_resp_send(req, msg, strlen(msg));  
    return ESP_OK;
}


esp_err_t sendDiscovery_and_static_Topics(void) {
    bool success = false;

    if (!sendingOf_DiscoveryAndStaticTopics_scheduled) {
        // Flag not set, nothing to do
        return ESP_OK;
    }

    if (HomeassistantDiscovery) {
        success = MQTThomeassistantDiscovery(1);
    }

    success |= publishStaticData(1);

    if (success) { // Success, clear the flag
        sendingOf_DiscoveryAndStaticTopics_scheduled = false;
        return ESP_OK;
    }
    else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "One or more MQTT topics failed to be published, will try sending them in the next round!");
        /* Keep sendingOf_DiscoveryAndStaticTopics_scheduled set so we can retry after the next round */
        return ESP_FAIL;
    }
}


void GotConnected(std::string maintopic, bool retainFlag) {
    // Nothing to do
}

void register_server_mqtt_uri(httpd_handle_t server) {
    httpd_uri_t uri = { };
    uri.method    = HTTP_GET;

    uri.uri       = "/mqtt_publish_discovery";
    uri.handler = APPLY_BASIC_AUTH_FILTER(scheduleSendingDiscovery_and_static_Topics);
    uri.user_ctx  = (void*) "";    
    httpd_register_uri_handler(server, &uri); 
}


std::string getTimeUnit(void) {
    return timeUnit;
}


void SetHomeassistantDiscoveryEnabled(bool enabled) {
    HomeassistantDiscovery = enabled;
}


void setMqtt_Server_Retain(bool _retainFlag) {
    retainFlag = _retainFlag;
}

void mqttServer_setMainTopic( std::string _maintopic) {
    maintopic = _maintopic;
}

std::string mqttServer_getMainTopic() {
    return maintopic;
}

void mqttServer_setDmoticzInTopic( std::string _domoticzintopic) {
    domoticzintopic = _domoticzintopic;
}


#endif //ENABLE_MQTT
