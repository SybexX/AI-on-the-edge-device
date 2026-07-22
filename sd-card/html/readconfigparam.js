// ============================================================================
// Global Configuration State
// ============================================================================
var config_gesamt = "";
var config_split = [];
var param = [];
var category;
var ref = new Array(2);
var NUMBERS = new Array(0);
var REFERENCES = new Array(0);

// ============================================================================
// Data Fetching Functions
// ============================================================================

function getNUMBERSList() {
    var domainname = getDomainname();
    var namenumberslist = "";

    var xhttp = new XMLHttpRequest();

    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            namenumberslist = xhttp.responseText;
        } else {
            console.warn("Failed to fetch NUMBERS list:", xhttp.statusText, xhttp.responseText);
        }
    });

    try {
        var url = domainname + '/editflow?task=namenumbers';
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {
        console.error("Error fetching NUMBERS list:", error);
    }

    namenumberslist = namenumberslist.split("\t");
    return namenumberslist;
}

function getDATAList() {
    var domainname = getDomainname();
    var datalist = "";

    var xhttp = new XMLHttpRequest();

    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            datalist = xhttp.responseText;
        } else {
            console.warn("Failed to fetch DATA list:", xhttp.statusText, xhttp.responseText);
        }
    });

    try {
        var url = domainname + '/editflow?task=data';
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {
        console.error("Error fetching DATA list:", error);
    }

    datalist = datalist.split("\t");
    datalist.pop();
    datalist.sort();

    return datalist;
}

function getTFLITEList() {
    var domainname = getDomainname();
    var tflitelist = "";

    var xhttp = new XMLHttpRequest();

    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            tflitelist = xhttp.responseText;
        } else {
            console.warn("Failed to fetch TFLITE list:", xhttp.statusText, xhttp.responseText);
        }
    });

    try {
        var url = domainname + '/editflow?task=tflite';
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {
        console.error("Error fetching TFLITE list:", error);
    }

    tflitelist = tflitelist.split("\t");
    tflitelist.sort();

    return tflitelist;
}

// ============================================================================
// Configuration Parsing
// ============================================================================

function ParseConfig() {
    config_split = config_gesamt.split("\n");
    var aktline = 0;

    param = new Object();
    category = new Object();

    // Initialize all configuration categories
    var catname = "TakeImage";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "RawImagesLocation", 1, false, "/log/source");
    ParamAddValue(param, catname, "RawImagesRetention", 1, false, "15");
    ParamAddValue(param, catname, "WaitBeforeTakingPicture", 1, false, "3");
    ParamAddValue(param, catname, "CamGainCeiling", 1, false, "x8");           // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
    ParamAddValue(param, catname, "CamQuality", 1, false, "10");               // 0 - 63
    ParamAddValue(param, catname, "CamBrightness", 1, false, "0");             // (-2 to 2) - set brightness
    ParamAddValue(param, catname, "CamContrast", 1, false, "0");               // -2 - 2
    ParamAddValue(param, catname, "CamSaturation", 1, false, "0");             // -2 - 2
    ParamAddValue(param, catname, "CamSharpness", 1, false, "0");              // -2 - 2
    ParamAddValue(param, catname, "CamAutoSharpness", 1, false, "false");      // (1 or 0)
    ParamAddValue(param, catname, "CamSpecialEffect", 1, false, "no_effect");  // 0 - 6
    ParamAddValue(param, catname, "CamWbMode", 1, false, "auto");              // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    ParamAddValue(param, catname, "CamAwb", 1, false, "true");                 // white balance enable (0 or 1)
    ParamAddValue(param, catname, "CamAwbGain", 1, false, "true");             // Auto White Balance enable (0 or 1)
    ParamAddValue(param, catname, "CamAec", 1, false, "true");                 // auto exposure off (1 or 0)
    ParamAddValue(param, catname, "CamAec2", 1, false, "true");                // automatic exposure sensor  (0 or 1)
    ParamAddValue(param, catname, "CamAeLevel", 1, false, "2");                // auto exposure levels (-2 to 2)
    ParamAddValue(param, catname, "CamAecValue", 1, false, "600");             // set exposure manually  (0-1200)
    ParamAddValue(param, catname, "CamAgc", 1, false, "true");                 // auto gain off (1 or 0)
    ParamAddValue(param, catname, "CamAgcGain", 1, false, "8");                // set gain manually (0 - 30)
    ParamAddValue(param, catname, "CamBpc", 1, false, "true");                 // black pixel correction
    ParamAddValue(param, catname, "CamWpc", 1, false, "true");                 // white pixel correction
    ParamAddValue(param, catname, "CamRawGma", 1, false, "true");              // (1 or 0)
    ParamAddValue(param, catname, "CamLenc", 1, false, "true");                // lens correction (1 or 0)
    ParamAddValue(param, catname, "CamHmirror", 1, false, "false");            // (0 or 1) flip horizontally
    ParamAddValue(param, catname, "CamVflip", 1, false, "false");              // Invert image (0 or 1)
    ParamAddValue(param, catname, "CamDcw", 1, false, "true");                 // downsize enable (1 or 0)
    ParamAddValue(param, catname, "CamDenoise", 1, false, "0");                // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)
    ParamAddValue(param, catname, "CamZoom", 1, false, "false");
    ParamAddValue(param, catname, "CamZoomOffsetX", 1, false, "0");
    ParamAddValue(param, catname, "CamZoomOffsetY", 1, false, "0");
    ParamAddValue(param, catname, "CamZoomSize", 1, false, "0");
    ParamAddValue(param, catname, "LEDIntensity", 1, false, "50");
    ParamAddValue(param, catname, "Demo", 1, false, "false");

    catname = "Alignment";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "InitialRotate", 1, false, "0");
    ParamAddValue(param, catname, "SearchFieldX", 1, false, "20");
    ParamAddValue(param, catname, "SearchFieldY", 1, false, "20");
    ParamAddValue(param, catname, "AlignmentAlgo", 1, false, "default");

    catname = "Digits";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Model");
    ParamAddValue(param, catname, "CNNGoodThreshold", 1, false, "0.7");
    ParamAddValue(param, catname, "ROIImagesLocation", 1, false, "/log/digit");
    ParamAddValue(param, catname, "ROIImagesRetention", 1, false, "3");

    catname = "Analog";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Model");
    ParamAddValue(param, catname, "ROIImagesLocation", 1, false, "/log/analog");
    ParamAddValue(param, catname, "ROIImagesRetention", 1, false, "3");

    catname = "PostProcessing";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "DecimalShift", 1, true, "0");
    ParamAddValue(param, catname, "AnalogToDigitTransitionStart", 1, true, "9.2");
    ParamAddValue(param, catname, "ChangeRateThreshold", 1, true, "2");
    ParamAddValue(param, catname, "PreValueUse");
    ParamAddValue(param, catname, "PreValueAgeStartup");
    ParamAddValue(param, catname, "AllowNegativeRates", 1, true, "false");
    ParamAddValue(param, catname, "MaxRateValue", 1, true, "0.05");
    ParamAddValue(param, catname, "MaxRateType", 1, true);
    ParamAddValue(param, catname, "ExtendedResolution", 1, true, "false");
    ParamAddValue(param, catname, "IgnoreLeadingNaN", 1, true, "false");
    ParamAddValue(param, catname, "ErrorMessage");
    ParamAddValue(param, catname, "CheckDigitIncreaseConsistency", 1, true, "false");

    catname = "MQTT";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Uri");
    ParamAddValue(param, catname, "MainTopic", 1, false);
    ParamAddValue(param, catname, "ClientID");
    ParamAddValue(param, catname, "user");
    ParamAddValue(param, catname, "password");
    ParamAddValue(param, catname, "RetainMessages");
    ParamAddValue(param, catname, "DomoticzTopicIn");
    ParamAddValue(param, catname, "DomoticzIDX", 1, true);
    ParamAddValue(param, catname, "HomeassistantDiscovery");
    ParamAddValue(param, catname, "MeterType");
    ParamAddValue(param, catname, "CACert");
    ParamAddValue(param, catname, "ClientCert");
    ParamAddValue(param, catname, "ClientKey");
    ParamAddValue(param, catname, "ValidateServerCert");

    catname = "InfluxDB";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Uri");
    ParamAddValue(param, catname, "Database");
    ParamAddValue(param, catname, "user");
    ParamAddValue(param, catname, "password");
    ParamAddValue(param, catname, "Measurement", 1, true);
    ParamAddValue(param, catname, "Field", 1, true);

    catname = "InfluxDBv2";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Uri");
    ParamAddValue(param, catname, "Bucket");
    ParamAddValue(param, catname, "Org");
    ParamAddValue(param, catname, "Token");
    ParamAddValue(param, catname, "Measurement", 1, true);
    ParamAddValue(param, catname, "Field", 1, true);

    catname = "Webhook";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Uri");
    ParamAddValue(param, catname, "ApiKey");
    ParamAddValue(param, catname, "UploadImg");

    catname = "GPIO";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "IO0", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "IO1", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "IO3", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "IO4", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "IO12", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "IO13", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(param, catname, "LEDType");
    ParamAddValue(param, catname, "LEDNumbers");
    ParamAddValue(param, catname, "LEDColor", 3);

    // Default Values for backwards compatibility
    param[catname]["LEDType"]["value1"] = "WS2812";
    param[catname]["LEDNumbers"]["value1"] = "2";
    param[catname]["LEDColor"]["value1"] = "50";
    param[catname]["LEDColor"]["value2"] = "50";
    param[catname]["LEDColor"]["value3"] = "50";

    catname = "AutoTimer";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Interval");

    catname = "DataLogging";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "DataLogActive");
    ParamAddValue(param, catname, "DataFilesRetention");

    catname = "Debug";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "LogLevel");
    ParamAddValue(param, catname, "LogfilesRetention");

    catname = "System";
    category[catname] = new Object();
    category[catname]["enabled"] = false;
    category[catname]["found"] = false;
    param[catname] = new Object();
    ParamAddValue(param, catname, "Tooltip");
    ParamAddValue(param, catname, "TimeZone");
    ParamAddValue(param, catname, "TimeServer");
    ParamAddValue(param, catname, "Hostname");
    ParamAddValue(param, catname, "RSSIThreshold");
    ParamAddValue(param, catname, "CPUFrequency");
    ParamAddValue(param, catname, "SetupMode");

    // Parse config lines
    while (aktline < config_split.length) {
        for (var cat in category) {
            var zw = cat.toUpperCase();
            var zw1 = "[" + zw + "]";
            var zw2 = ";[" + zw + "]";

            if ((config_split[aktline].trim().toUpperCase() == zw1) || (config_split[aktline].trim().toUpperCase() == zw2)) {
                if (config_split[aktline].trim().toUpperCase() == zw1) {
                    category[cat]["enabled"] = true;
                }

                category[cat]["found"] = true;
                category[cat]["line"] = aktline;
                aktline = ParseConfigParamAll(aktline, cat);
                continue;
            }
        }

        aktline++;
    }

    // Apply default values to any missing parameters
    ApplyDefaultValues();
}

function ParamAddValue(param, cat, paramName, anzParam = 1, isNUMBER = false, defaultValue = "", checkRegExList = null) {
    param[cat][paramName] = new Object();
    param[cat][paramName]["found"] = false;
    param[cat][paramName]["enabled"] = false;
    param[cat][paramName]["line"] = -1;
    param[cat][paramName]["anzParam"] = anzParam;
    param[cat][paramName]["defaultValue"] = defaultValue;
    param[cat][paramName]["Numbers"] = isNUMBER;
    param[cat][paramName].checkRegExList = checkRegExList;
}

/**
 * Apply default values to parameters that were not found in the config file.
 * This is called after parsing to ensure all parameters have values.
 */
function ApplyDefaultValues() {
    for (var cat in param) {
        for (var paramName in param[cat]) {
            // Skip if parameter was already found in config
            if (param[cat][paramName]["found"] === true) {
                continue;
            }

            var defaultValue = param[cat][paramName]["defaultValue"];
            var anzParam = param[cat][paramName]["anzParam"];

            // If there's a default value, apply it
            if (defaultValue !== "" && defaultValue !== null && defaultValue !== undefined) {
                param[cat][paramName]["found"] = true;
                param[cat][paramName]["enabled"] = true;

                // For single value parameters
                if (anzParam === 1) {
                    param[cat][paramName]["value1"] = defaultValue;
                } else {
                    // For multi-value parameters, apply to first value
                    param[cat][paramName]["value1"] = defaultValue;
                }

                console.log("Applied default value for [" + cat + "] " + paramName + " = " + defaultValue);
            }
        }
    }

    // Apply default values for NUMBER sequences
    for (var num in NUMBERS) {
        for (var cat in param) {
            for (var paramName in param[cat]) {
                // Only process parameters marked as Numbers
                if (param[cat][paramName]["Numbers"] !== true) {
                    continue;
                }

                // Skip if already found
                if (NUMBERS[num][cat] && NUMBERS[num][cat][paramName] && NUMBERS[num][cat][paramName]["found"] === true) {
                    continue;
                }

                var defaultValue = param[cat][paramName]["defaultValue"];

                // If there's a default value, apply it
                if (defaultValue !== "" && defaultValue !== null && defaultValue !== undefined) {
                    if (!NUMBERS[num][cat]) {
                        NUMBERS[num][cat] = new Object();
                    }

                    if (!NUMBERS[num][cat][paramName]) {
                        NUMBERS[num][cat][paramName] = new Object();
                    }

                    NUMBERS[num][cat][paramName]["found"] = true;
                    NUMBERS[num][cat][paramName]["enabled"] = true;
                    NUMBERS[num][cat][paramName]["value1"] = defaultValue;

                    console.log("Applied default value for [" + cat + "] " + NUMBERS[num]["name"] + "." + paramName + " = " + defaultValue);
                }
            }
        }
    }
}

function ParseConfigParamAll(aktline, catname) {
    ++aktline;

    while ((aktline < config_split.length) && !(config_split[aktline][0] == "[") && !((config_split[aktline][0] == ";") && (config_split[aktline][1] == "["))) {
        var input = config_split[aktline];
        var [isCom, cleanInput] = isCommented(input);
        var linesplit = ZerlegeZeile(cleanInput);
        ParamExtractValueAll(param, linesplit, catname, aktline, isCom);

        if (!isCom && (linesplit.length >= 5) && (catname == 'Digits')) {
            ExtractROIs(cleanInput, "digit");
        }

        if (!isCom && (linesplit.length >= 5) && (catname == 'Analog')) {
            ExtractROIs(cleanInput, "analog");
        }

        if (!isCom && (linesplit.length == 3) && (catname == 'Alignment')) {
            var newref = new Object();
            newref["name"] = linesplit[0];
            newref["x"] = linesplit[1];
            newref["y"] = linesplit[2];
            REFERENCES.push(newref);
        }

        ++aktline;
    }

    return aktline;
}

function ParamExtractValueAll(param, linesplit, catname, aktline, isCom) {
    for (var paramname in param[catname]) {
        var aktROI = "default";
        var aktPara = linesplit[0];
        var posPunkt = aktPara.indexOf(".");

        if (posPunkt > -1) {
            aktROI = aktPara.substring(0, posPunkt);
            aktPara = aktPara.substring(posPunkt + 1);
        }

        if (aktPara.toUpperCase() == paramname.toUpperCase()) {
            while (linesplit.length <= param[catname][paramname]["anzParam"]) {
                linesplit.push("");
            }

            param[catname][paramname]["found"] = true;
            param[catname][paramname]["enabled"] = !isCom;
            param[catname][paramname]["line"] = aktline;

            if (param[catname][paramname]["Numbers"] == true) {
                // Multi-number support
                var numberEntry = getNUMBERS(linesplit[0]);
                numberEntry[catname][paramname] = new Object();
                numberEntry[catname][paramname]["found"] = true;
                numberEntry[catname][paramname]["enabled"] = !isCom;

                for (var j = 1; j <= param[catname][paramname]["anzParam"]; ++j) {
                    numberEntry[catname][paramname]["value" + j] = linesplit[j];
                }

                // Apply defaults if this is the "default" entry
                if (numberEntry["name"] == "default") {
                    for (var num in NUMBERS) {
                        if (NUMBERS[num][catname][paramname]["found"] == false) {
                            NUMBERS[num][catname][paramname]["found"] = true;
                            NUMBERS[num][catname][paramname]["enabled"] = !isCom;
                            NUMBERS[num][catname][paramname]["line"] = aktline;

                            for (var j = 1; j <= param[catname][paramname]["anzParam"]; ++j) {
                                NUMBERS[num][catname][paramname]["value" + j] = linesplit[j];
                            }
                        }
                    }
                }
            } else {
                // Single value parameter
                for (var j = 1; j <= param[catname][paramname]["anzParam"]; ++j) {
                    param[catname][paramname]["value" + j] = linesplit[j];
                }
            }
        }
    }
}

// ============================================================================
// Configuration Serialization
// ============================================================================

function getConfigParameters() {
    return param;
}

function WriteConfigININew() {
    // Cleanup empty NUMBERS entries
    for (var j = 0; j < NUMBERS.length; ++j) {
        if ((NUMBERS[j]["digit"].length + NUMBERS[j]["analog"].length) == 0) {
            NUMBERS.splice(j, 1);
        }
    }

    config_split = new Array(0);

    for (var cat in param) {
        var text = "[" + cat + "]";

        if (!category[cat]["enabled"]) {
            text = ";" + text;
        }

        config_split.push(text);

        for (var name in param[cat]) {
            if (param[cat][name]["Numbers"]) {
                // Multi-number parameters
                for (var num in NUMBERS) {
                    text = NUMBERS[num]["name"] + "." + name;
                    text = text + " =";

                    for (var j = 1; j <= param[cat][name]["anzParam"]; ++j) {
                        if (!(typeof NUMBERS[num][cat][name]["value" + j] == 'undefined')) {
                            text = text + " " + NUMBERS[num][cat][name]["value" + j];
                        }
                    }

                    if (!NUMBERS[num][cat][name]["enabled"]) {
                        text = ";" + text;
                    }

                    config_split.push(text);
                }
            } else {
                // Single-value parameters
                text = name + " =";

                for (var j = 1; j <= param[cat][name]["anzParam"]; ++j) {
                    if (!(typeof param[cat][name]["value" + j] == 'undefined')) {
                        text = text + " " + param[cat][name]["value" + j];
                    }
                }

                // Determine if parameter should be commented
                var shouldComment = !param[cat][name]["enabled"];
                var hasDefaultValue = param[cat][name]["defaultValue"] !== "" && 
                                     param[cat][name]["defaultValue"] !== null && 
                                     param[cat][name]["defaultValue"] !== undefined;

                // Don't comment parameters that have default values
                if (shouldComment && hasDefaultValue) {
                    // Parameter has a default value, so don't comment it
                    // Make sure the value is set to the default
                    if (!(typeof param[cat][name]["value1"] !== 'undefined' && param[cat][name]["value1"] !== "")) {
                        text = name + " = " + param[cat][name]["defaultValue"];
                    }
                    config_split.push(text);
                } else if (shouldComment && !hasDefaultValue) {
                    // No default value and disabled - comment it out
                    text = ";" + text;
                    config_split.push(text);
                } else {
                    // Parameter is enabled
                    config_split.push(text);
                }
            }
        }

        // Append ROI definitions for Digits category
        if (cat == "Digits") {
            for (var roi in NUMBERS) {
                if (NUMBERS[roi]["digit"].length > 0) {
                    for (var roidet in NUMBERS[roi]["digit"]) {
                        text = NUMBERS[roi]["name"] + "." + NUMBERS[roi]["digit"][roidet]["name"];
                        text = text + " " + NUMBERS[roi]["digit"][roidet]["x"];
                        text = text + " " + NUMBERS[roi]["digit"][roidet]["y"];
                        text = text + " " + NUMBERS[roi]["digit"][roidet]["dx"];
                        text = text + " " + NUMBERS[roi]["digit"][roidet]["dy"];
                        text = text + " " + NUMBERS[roi]["digit"][roidet]["CCW"];
                        config_split.push(text);
                    }
                }
            }
        }

        // Append ROI definitions for Analog category
        if (cat == "Analog") {
            for (var roi in NUMBERS) {
                if (NUMBERS[roi]["analog"].length > 0) {
                    for (var roidet in NUMBERS[roi]["analog"]) {
                        text = NUMBERS[roi]["name"] + "." + NUMBERS[roi]["analog"][roidet]["name"];
                        text = text + " " + NUMBERS[roi]["analog"][roidet]["x"];
                        text = text + " " + NUMBERS[roi]["analog"][roidet]["y"];
                        text = text + " " + NUMBERS[roi]["analog"][roidet]["dx"];
                        text = text + " " + NUMBERS[roi]["analog"][roidet]["dy"];
                        text = text + " " + NUMBERS[roi]["analog"][roidet]["CCW"];
                        config_split.push(text);
                    }
                }
            }
        }

        // Append reference definitions for Alignment category
        if (cat == "Alignment") {
            for (var roi in REFERENCES) {
                text = REFERENCES[roi]["name"];
                text = text + " " + REFERENCES[roi]["x"];
                text = text + " " + REFERENCES[roi]["y"];
                config_split.push(text);
            }
        }

        config_split.push("");
    }
}

function isCommented(input) {
    var isComment = false;

    if (input.charAt(0) == ';') {
        isComment = true;
        input = input.substr(1, input.length - 1);
    }

    return [isComment, input];
}

function getConfig() {
    return config_gesamt;
}

function getConfigCategory() {
    return category;
}

// ============================================================================
// ROI Management
// ============================================================================

function ExtractROIs(aktline, type) {
    var linesplit = ZerlegeZeile(aktline);
    var numberEntry = getNUMBERS(linesplit[0], type);
    numberEntry["pos_ref"] = aktline;
    numberEntry["x"] = linesplit[1];
    numberEntry["y"] = linesplit[2];
    numberEntry["dx"] = linesplit[3];
    numberEntry["dy"] = linesplit[4];
    numberEntry["ar"] = parseFloat(linesplit[3]) / parseFloat(linesplit[4]);
    numberEntry["CCW"] = "false";

    if (linesplit.length >= 6) {
        numberEntry["CCW"] = linesplit[5];
    }
}

function getNUMBERS(name, type, create = true) {
    var posPunkt = name.indexOf(".");
    var digit, roi;

    if (posPunkt > -1) {
        digit = name.substring(0, posPunkt);
        roi = name.substring(posPunkt + 1);
    } else {
        digit = "default";
        roi = name;
    }

    var ret = -1;

    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == digit) {
            ret = NUMBERS[i];
        }
    }

    if (!create) {
        // Return as-is (may be -1 if not found)
        return ret;
    }

    if (ret == -1) {
        ret = new Object();
        ret["name"] = digit;
        ret['digit'] = new Array();
        ret['analog'] = new Array();

        for (var cat in param) {
            for (var paramItem in param[cat]) {
                if (param[cat][paramItem]["Numbers"] == true) {
                    if (typeof ret[cat] == 'undefined') {
                        ret[cat] = new Object();
                    }

                    ret[cat][paramItem] = new Object();
                    ret[cat][paramItem]["found"] = false;
                    ret[cat][paramItem]["enabled"] = false;
                    ret[cat][paramItem]["anzParam"] = param[cat][paramItem]["anzParam"];
                }
            }
        }

        NUMBERS.push(ret);
    }

    if (typeof type == 'undefined') {
        // Must already exist - call only after Digits/Analog parsing
        return ret;
    }

    var newroi = new Object();
    newroi["name"] = roi;
    ret[type].push(newroi);

    return newroi;
}

// ============================================================================
// Reference Management
// ============================================================================

function CopyReferenceToImgTmp(domainname) {
    for (var index = 0; index < 2; ++index) {
        var filenamevon = REFERENCES[index]["name"];
        var filenamenach = filenamevon.replace("/config/", "/img_tmp/");
        FileDeleteOnServer(filenamenach, domainname);
        FileCopyOnServer(filenamevon, filenamenach, domainname);

        filenamevon = filenamevon.replace(".jpg", "_org.jpg");
        filenamenach = filenamenach.replace(".jpg", "_org.jpg");
        FileDeleteOnServer(filenamenach, domainname);
        FileCopyOnServer(filenamevon, filenamenach, domainname);
    }
}

function GetReferencesInfo() {
    return REFERENCES;
}

function UpdateConfigReferences(domainname) {
    for (var index = 0; index < 2; ++index) {
        var filenamenach = REFERENCES[index]["name"];
        var filenamevon = filenamenach.replace("/config/", "/img_tmp/");
        FileDeleteOnServer(filenamenach, domainname);
        FileCopyOnServer(filenamevon, filenamenach, domainname);

        filenamenach = filenamenach.replace(".jpg", "_org.jpg");
        filenamevon = filenamevon.replace(".jpg", "_org.jpg");
        FileDeleteOnServer(filenamenach, domainname);
        FileCopyOnServer(filenamevon, filenamenach, domainname);
    }
}

function UpdateConfigReference(anzneueref, domainname) {
    var index = 0;

    if (anzneueref == 1) {
        index = 0;
    } else if (anzneueref == 2) {
        index = 1;
    }

    var filenamenach = REFERENCES[index]["name"];
    var filenamevon = filenamenach.replace("/config/", "/img_tmp/");

    FileDeleteOnServer(filenamenach, domainname);
    FileCopyOnServer(filenamevon, filenamenach, domainname);

    filenamenach = filenamenach.replace(".jpg", "_org.jpg");
    filenamevon = filenamevon.replace(".jpg", "_org.jpg");

    FileDeleteOnServer(filenamenach, domainname);
    FileCopyOnServer(filenamevon, filenamenach, domainname);
}

// ============================================================================
// NUMBER Sequence Management
// ============================================================================

function getNUMBERInfo() {
    return NUMBERS;
}

function RenameNUMBER(oldName, newName) {
    if ((newName.indexOf(".") >= 0) || (newName.indexOf(",") >= 0) || (newName.indexOf(" ") >= 0) || (newName.indexOf("\"") >= 0)) {
        return "Number sequence name must not contain , . \" or a space";
    }

    var index = -1;
    var found = false;

    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == oldName) {
            index = i;
        }

        if (NUMBERS[i]["name"] == newName) {
            found = true;
        }
    }

    if (found) {
        return "Number sequence name is already existing, please choose another name";
    }

    if (index > -1) {
        NUMBERS[index]["name"] = newName;
    }

    return "";
}

function DeleteNUMBER(numberName) {
    if (NUMBERS.length == 1) {
        return "One number sequence is mandatory. Therefore this cannot be deleted";
    }

    var index = -1;

    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == numberName) {
            index = i;
        }
    }

    if (index > -1) {
        NUMBERS.splice(index, 1);
    }

    return "";
}

function CreateNUMBER(numbernew) {
    var found = false;

    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == numbernew) {
            found = true;
        }
    }

    if (found) {
        return "Number sequence name is already existing, please choose another name";
    }

    var ret = new Object();
    ret["name"] = numbernew;
    ret['digit'] = new Array();
    ret['analog'] = new Array();

    for (var cat in param) {
        for (var paramItem in param[cat]) {
            if (param[cat][paramItem]["Numbers"] == true) {
                if (typeof (ret[cat]) === "undefined") {
                    ret[cat] = new Object();
                }

                ret[cat][paramItem] = new Object();

                if (param[cat][paramItem]["defaultValue"] === "") {
                    ret[cat][paramItem]["found"] = false;
                    ret[cat][paramItem]["enabled"] = false;
                } else {
                    ret[cat][paramItem]["found"] = true;
                    ret[cat][paramItem]["enabled"] = true;
                    ret[cat][paramItem]["value1"] = param[cat][paramItem]["defaultValue"];
                }

                ret[cat][paramItem]["anzParam"] = param[cat][paramItem]["anzParam"];
            }
        }
    }

    NUMBERS.push(ret);
    return "";
}

// ============================================================================
// ROI Query and Management
// ============================================================================

function getROIInfo(typeROI, number) {
    var index = -1;

    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == number) {
            index = i;
        }
    }

    if (index != -1) {
        return NUMBERS[index][typeROI];
    } else {
        return "";
    }
}

function RenameROI(number, type, oldName, newName) {
    if ((newName.includes("=")) || (newName.includes(".")) || (newName.includes(":")) || (newName.includes(",")) || (newName.includes(";")) || (newName.includes(" ")) || (newName.includes("\""))) {
        return "ROI name must not contain . : , ; = \" or space";
    }

    var index = -1;
    var found = false;
    var indexnumber = -1;

    for (var j = 0; j < NUMBERS.length; ++j) {
        if (NUMBERS[j]["name"] == number) {
            indexnumber = j;
        }
    }

    if (indexnumber == -1) {
        return "Number sequence not existing. ROI cannot be renamed";
    }

    for (var i = 0; i < NUMBERS[indexnumber][type].length; ++i) {
        if (NUMBERS[indexnumber][type][i]["name"] == oldName) {
            index = i;
        }

        if (NUMBERS[indexnumber][type][i]["name"] == newName) {
            found = true;
        }
    }

    if (found) {
        return "ROI name is already existing, please choose another name";
    }

    if (index > -1) {
        NUMBERS[indexnumber][type][index]["name"] = newName;
    }

    return "";
}

function CreateROI(number, type, pos, roinew, x, y, dx, dy, ccw) {
    var indexnumber = -1;

    for (var j = 0; j < NUMBERS.length; ++j) {
        if (NUMBERS[j]["name"] == number) {
            indexnumber = j;
        }
    }

    if (indexnumber == -1) {
        return "Number sequence not existing. ROI cannot be created";
    }

    var found = false;

    for (var i = 0; i < NUMBERS[indexnumber][type].length; ++i) {
        if (NUMBERS[indexnumber][type][i]["name"] == roinew) {
            found = true;
        }
    }

    if (found) {
        return "ROI name is already existing, please choose another name";
    }

    var ret = new Object();
    ret["name"] = roinew;
    ret["x"] = x;
    ret["y"] = y;
    ret["dx"] = dx;
    ret["dy"] = dy;
    ret["ar"] = dx / dy;
    ret["CCW"] = ccw;

    NUMBERS[indexnumber][type].splice(pos + 1, 0, ret);

    return "";
}
