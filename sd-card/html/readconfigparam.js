var config_gesamt = "";
var config_split = "";
var config_param = [];
var config_category = [];

var namenumberslist = "";
var datalist = "";
var tflitelist = "";

var NUMBERS = new Array(0);
var REFERENCES = new Array(0);

function loadConfig(_domainname) {
    _domainname = getDomainname();
	config_gesamt = "";
	
    var xhttp = new XMLHttpRequest();
    
	try {
        url = _domainname + '/fileserver/config/config.ini';     
        xhttp.open("GET", url, false);
        xhttp.send();
        config_gesamt = xhttp.responseText;
    } catch (error) {}
    
	return true;
}

function getNUMBERSList() {
    _domainname = getDomainname(); 
    namenumberslist = "";

    var xhttp = new XMLHttpRequest();
	
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            namenumberslist = xhttp.responseText;
        } 
        else {
            console.warn(request.statusText, request.responseText);
        }
    });

    try {
        url = _domainname + '/editflow?task=namenumbers';     
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {}

    namenumberslist = namenumberslist.split("\t");

    return namenumberslist;
}

function getDATAList() {
    _domainname = getDomainname(); 
    datalist = "";

    var xhttp = new XMLHttpRequest();
	
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            datalist = xhttp.responseText;
        } 
        else {
            console.warn(request.statusText, request.responseText);
        }
    });

    try {
        url = _domainname + '/editflow?task=data';     
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {}

    datalist = datalist.split("\t");
    datalist.pop();
    datalist.sort();

    return datalist;
}

function getTFLITEList() {
    _domainname = getDomainname(); 
    tflitelist = "";

    var xhttp = new XMLHttpRequest();
	
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            tflitelist = xhttp.responseText;
        } 
        else {
            console.warn(request.statusText, request.responseText);
        }
    });

    try {
        url = _domainname + '/editflow?task=tflite';
        xhttp.open("GET", url, false);
        xhttp.send();
    } catch (error) {}

    tflitelist = tflitelist.split("\t");
    tflitelist.sort();

    return tflitelist;
}

function ParseConfig() {
    config_split = config_gesamt.split("\n");
    var aktline = 0;

    config_param = new Object();
    config_category = new Object(); 

    var catname = "TakeImage";
    config_category[catname] = new Object(); 
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "RawImagesLocation", 1, false, "/log/source");
    ParamAddValue(config_param, catname, "RawImagesRetention", 1, false, "15");
    ParamAddValue(config_param, catname, "SaveAllFiles", 1, false, "false");
    ParamAddValue(config_param, catname, "WaitBeforeTakingPicture", 1, false, "2");
    ParamAddValue(config_param, catname, "CamXclkFreqMhz", 1, false, "20");
    ParamAddValue(config_param, catname, "CamGainceiling", 1, false, "x8");            // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
    ParamAddValue(config_param, catname, "CamQuality", 1, false, "10");                // 0 - 63
    ParamAddValue(config_param, catname, "CamBrightness", 1, false, "0");              // (-2 to 2) - set brightness
    ParamAddValue(config_param, catname, "CamContrast", 1, false, "0");                //-2 - 2
    ParamAddValue(config_param, catname, "CamSaturation", 1, false, "0");              //-2 - 2
    ParamAddValue(config_param, catname, "CamSharpness", 1, false, "0");               //-2 - 2
    ParamAddValue(config_param, catname, "CamAutoSharpness", 1, false, "false");       // (1 or 0)	
    ParamAddValue(config_param, catname, "CamSpecialEffect", 1, false, "no_effect");   // 0 - 6
    ParamAddValue(config_param, catname, "CamWbMode", 1, false, "auto");               // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    ParamAddValue(config_param, catname, "CamAwb", 1, false, "true");                  // white balance enable (0 or 1)
    ParamAddValue(config_param, catname, "CamAwbGain", 1, false, "true");              // Auto White Balance enable (0 or 1)
    ParamAddValue(config_param, catname, "CamAec", 1, false, "true");                  // auto exposure off (1 or 0)
    ParamAddValue(config_param, catname, "CamAec2", 1, false, "true");                 // automatic exposure sensor  (0 or 1)
    ParamAddValue(config_param, catname, "CamAeLevel", 1, false, "2");                 // auto exposure levels (-2 to 2)
    ParamAddValue(config_param, catname, "CamAecValue", 1, false, "600");              // set exposure manually  (0-1200)
    ParamAddValue(config_param, catname, "CamAgc", 1, false, "true");                  // auto gain off (1 or 0)
    ParamAddValue(config_param, catname, "CamAgcGain", 1, false, "8");                 // set gain manually (0 - 30)
    ParamAddValue(config_param, catname, "CamBpc", 1, false, "true");                  // black pixel correction
    ParamAddValue(config_param, catname, "CamWpc", 1, false, "true");                  // white pixel correction
    ParamAddValue(config_param, catname, "CamRawGma", 1, false, "true");               // (1 or 0)
    ParamAddValue(config_param, catname, "CamLenc", 1, false, "true");                 // lens correction (1 or 0)
    ParamAddValue(config_param, catname, "CamHmirror", 1, false, "false");             // (0 or 1) flip horizontally
    ParamAddValue(config_param, catname, "CamVflip", 1, false, "false");               // Invert image (0 or 1)
    ParamAddValue(config_param, catname, "CamDcw", 1, false, "true");                  // downsize enable (1 or 0)
    ParamAddValue(config_param, catname, "CamDenoise", 1, false, "0");                 // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)
    ParamAddValue(config_param, catname, "CamZoom", 1, false, "false");
    ParamAddValue(config_param, catname, "CamZoomSize", 1, false, "0");
    ParamAddValue(config_param, catname, "CamZoomOffsetX", 1, false, "0");
    ParamAddValue(config_param, catname, "CamZoomOffsetY", 1, false, "0");
    ParamAddValue(config_param, catname, "LEDIntensity", 1, false, "50");
    ParamAddValue(config_param, catname, "Demo", 1, false, "false");

    var catname = "Alignment";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "SearchFieldX", 1, false, "20");
    ParamAddValue(config_param, catname, "SearchFieldY", 1, false, "20");
    ParamAddValue(config_param, catname, "SearchMaxAngle", 1, false, "15");
    ParamAddValue(config_param, catname, "Antialiasing", 1, false, "true");
    ParamAddValue(config_param, catname, "AlignmentAlgo", 1, false, "default");
    ParamAddValue(config_param, catname, "InitialRotate", 1, false, "0");

    var catname = "Digits";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Model");
    ParamAddValue(config_param, catname, "CNNGoodThreshold", 1, false, "0.5");
    ParamAddValue(config_param, catname, "ROIImagesLocation", 1, false, "/log/digit");
    ParamAddValue(config_param, catname, "ROIImagesRetention", 1, false, "3");

    var catname = "Analog";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Model");
    ParamAddValue(config_param, catname, "ROIImagesLocation", 1, false, "/log/analog");
    ParamAddValue(config_param, catname, "ROIImagesRetention", 1, false, "3");

    var catname = "PostProcessing";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    // ParamAddValue(config_param, catname, "PreValueUse", 1, true, "true");
    ParamAddValue(config_param, catname, "PreValueUse", 1, false, "true");
    ParamAddValue(config_param, catname, "PreValueAgeStartup", 1, false, "720");
    ParamAddValue(config_param, catname, "ErrorMessage");
    ParamAddValue(config_param, catname, "AllowNegativeRates", 1, true, "false");
    ParamAddValue(config_param, catname, "DecimalShift", 1, true, "0");
    ParamAddValue(config_param, catname, "AnalogToDigitTransitionStart", 1, true, "9.2");
    ParamAddValue(config_param, catname, "MaxFlowRate", 1, true, "4.0");
    ParamAddValue(config_param, catname, "MaxRateValue", 1, true, "0.05");
    ParamAddValue(config_param, catname, "MaxRateType", 1, true);
    ParamAddValue(config_param, catname, "ChangeRateThreshold", 1, true, "2");
    ParamAddValue(config_param, catname, "ExtendedResolution", 1, true, "false");
    ParamAddValue(config_param, catname, "IgnoreLeadingNaN", 1, true, "false");
    ParamAddValue(config_param, catname, "CheckDigitIncreaseConsistency", 1, true, "false");

    var catname = "MQTT";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Uri");
    ParamAddValue(config_param, catname, "MainTopic", 1, false);
    ParamAddValue(config_param, catname, "ClientID");
    ParamAddValue(config_param, catname, "user");
    ParamAddValue(config_param, catname, "password");
    ParamAddValue(config_param, catname, "RetainMessages");
    ParamAddValue(config_param, catname, "DomoticzTopicIn");
    ParamAddValue(config_param, catname, "DomoticzIDX", 1, true);
    ParamAddValue(config_param, catname, "HomeassistantDiscovery");
    ParamAddValue(config_param, catname, "MeterType");
    ParamAddValue(config_param, catname, "CACert");
    ParamAddValue(config_param, catname, "ClientCert");
    ParamAddValue(config_param, catname, "ClientKey");
    ParamAddValue(config_param, catname, "ValidateServerCert");

    var catname = "InfluxDB";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Uri");
    ParamAddValue(config_param, catname, "Database");
    // ParamAddValue(config_param, catname, "Measurement");
    ParamAddValue(config_param, catname, "user");
    ParamAddValue(config_param, catname, "password");
    ParamAddValue(config_param, catname, "Measurement", 1, true);
    ParamAddValue(config_param, catname, "Field", 1, true);

    var catname = "InfluxDBv2";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Uri");
    ParamAddValue(config_param, catname, "Bucket");
    // ParamAddValue(config_param, catname, "Measurement");
    ParamAddValue(config_param, catname, "Org");
    ParamAddValue(config_param, catname, "Token");
    ParamAddValue(config_param, catname, "Measurement", 1, true);
    ParamAddValue(config_param, catname, "Field", 1, true);

    var catname = "Webhook";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Uri");
    ParamAddValue(config_param, catname, "ApiKey");
    ParamAddValue(config_param, catname, "UploadImg");

    var catname = "GPIO";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "IO0", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "IO1", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "IO3", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "IO4", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "IO12", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "IO13", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
    ParamAddValue(config_param, catname, "LEDType");
    ParamAddValue(config_param, catname, "LEDNumbers");
    ParamAddValue(config_param, catname, "LEDColor", 3);

    var catname = "AutoTimer";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    //ParamAddValue(config_param, catname, "AutoStart");
    ParamAddValue(config_param, catname, "Interval");     

    var catname = "DataLogging";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "DataLogActive");
    ParamAddValue(config_param, catname, "DataFilesRetention");     

    var catname = "Debug";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "LogLevel");
    ParamAddValue(config_param, catname, "LogfilesRetention");

    var catname = "System";
    config_category[catname] = new Object();
    config_category[catname]["enabled"] = false;
    config_category[catname]["found"] = false;
    config_param[catname] = new Object();
    ParamAddValue(config_param, catname, "Tooltip");	
    ParamAddValue(config_param, catname, "TimeZone");
    ParamAddValue(config_param, catname, "TimeServer");         
    ParamAddValue(config_param, catname, "Hostname");   
    ParamAddValue(config_param, catname, "RSSIThreshold");   
    ParamAddValue(config_param, catname, "CPUFrequency");
    ParamAddValue(config_param, catname, "SetupMode"); 
     
    while (aktline < config_split.length){
        for (var cat in config_category) {
            var cat_temp = cat.toUpperCase();
            var cat_aktive = "[" + cat_temp + "]";
            var cat_inaktive = ";[" + cat_temp + "]";
            
            if ((config_split[aktline].trim().toUpperCase() == cat_aktive) || (config_split[aktline].trim().toUpperCase() == cat_inaktive)) {
                if (config_split[aktline].trim().toUpperCase() == cat_aktive) {
                    config_category[cat]["enabled"] = true;
                }
                
                config_category[cat]["found"] = true;
                config_category[cat]["line"] = aktline;
                aktline = ParseConfigParamAll(aktline, cat);
                continue;
            }
        }
        
        aktline++;
    }
}

function ParamAddValue(_config_param, _config_category, _name, _anzParam = 1, _isNUMBER = false, _defaultValue = "", _checkRegExList = null) {
    _config_param[_config_category][_name] = new Object(); 
    _config_param[_config_category][_name]["found"] = false;
    _config_param[_config_category][_name]["enabled"] = false;
    _config_param[_config_category][_name]["line"] = -1; 
    _config_param[_config_category][_name]["anzParam"] = _anzParam;
    _config_param[_config_category][_name]["defaultValue"] = _defaultValue;
    _config_param[_config_category][_name]["Numbers"] = _isNUMBER;
    _config_param[_config_category][_name].checkRegExList = _checkRegExList;
	
    if (_isNUMBER) {
        for (var _num in NUMBERS) {
            for (var j = 1; j <= _config_param[_config_category][_name]["anzParam"]; ++j) {
                NUMBERS[_num][_config_category][_name]["value"+j] = _defaultValue;
            }
        }
    }
    else {
        for (var j = 1; j <= _config_param[_config_category][_name]["anzParam"]; ++j) {
            _config_param[_config_category][_name]["value"+j] = _defaultValue;
        }
    }
};

function ParseConfigParamAll(_aktline, _catname) {
    ++_aktline;

    while ((_aktline < config_split.length) && !(config_split[_aktline][0] == "[") && !((config_split[_aktline][0] == ";") && (config_split[_aktline][1] == "["))) {
        var _input = config_split[_aktline];
        let [isCom, input] = isCommented(_input);
        var linesplit = split_line(input);
        ParamExtractValueAll(config_param, linesplit, _catname, _aktline, isCom);
        
        if (!isCom && (linesplit.length >= 5) && (_catname == 'Digits')) {
            ExtractROIs(input, "digit");
        }
        
        if (!isCom && (linesplit.length >= 5) && (_catname == 'Analog')) {
            ExtractROIs(input, "analog");
        }
        
        if (!isCom && (linesplit.length == 3) && (_catname == 'Alignment')) {
            _newref = new Object();
            _newref["name"] = linesplit[0];
            _newref["x"] = linesplit[1];
            _newref["y"] = linesplit[2];
            REFERENCES.push(_newref);
        }

        ++_aktline;
    }
	
    return _aktline; 
}

function ParamExtractValue(_param, _linesplit, _catname, _paramname, _aktline, _iscom, _anzvalue = 1) {
    if ((_linesplit[0].toUpperCase() == _paramname.toUpperCase()) && (_linesplit.length > _anzvalue)) {
        _param[_catname][_paramname]["found"] = true;
        _param[_catname][_paramname]["enabled"] = !_iscom;
        _param[_catname][_paramname]["line"] = _aktline;
        _param[_catname][_paramname]["anzpara"] = _anzvalue;
        
        for (var j = 1; j <= _anzvalue; ++j) {
            _param[_catname][_paramname]["value"+j] = _linesplit[j];
        }
    }
}

function ParamExtractValueAll(_param, _linesplit, _catname, _aktline, _iscom) {
    for (var paramname in _param[_catname]) {
        _AktROI = "default";
        _AktPara = _linesplit[0];
        _pospunkt = _AktPara.indexOf (".");
        
        if (_pospunkt > -1) {
            _AktROI = _AktPara.substring(0, _pospunkt);
            _AktPara = _AktPara.substring(_pospunkt+1);
        }
        
        if (_AktPara.toUpperCase() == paramname.toUpperCase()) {
            while (_linesplit.length <= _param[_catname][paramname]["anzParam"]) {
                // line contains no value, so the default value is loaded
                _linesplit.push(_param[_catname][paramname]["defaultValue"]);
            }

            _param[_catname][paramname]["found"] = true;
            _param[_catname][paramname]["enabled"] = !_iscom;
            _param[_catname][paramname]["line"] = _aktline;
            
            if (_param[_catname][paramname]["Numbers"] == true) {
                // möglicher Multiusage
                var _numbers = getNUMBERS(_linesplit[0]);
                _numbers[_catname][paramname] = new Object;
                _numbers[_catname][paramname]["found"] = true;
                _numbers[_catname][paramname]["enabled"] = !_iscom;
     
                for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                    _numbers[_catname][paramname]["value"+j] = _linesplit[j];
                }
				
                if (_numbers["name"] == "default") {
                    for (var _num in NUMBERS) {
                        // Assign value to default
                        if (NUMBERS[_num][_catname][paramname]["found"] == false) {
                            NUMBERS[_num][_catname][paramname]["found"] = true;
                            NUMBERS[_num][_catname][paramname]["enabled"] = !_iscom;
                            NUMBERS[_num][_catname][paramname]["line"] = _aktline;
                                
                            for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                                NUMBERS[_num][_catname][paramname]["value"+j] = _linesplit[j];
                            }
                        }
                    }
                }
            }
            else {  
                for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                    _param[_catname][paramname]["value"+j] = _linesplit[j];
                }
            }
        }
    }
}

function getConfigParameters() {
	// loadConfig();
	ParseConfig();
	
    return config_param;
}

function getConfig() {
    return config_gesamt;
}

function getConfigCategory() {
    return config_category;
}

function WriteConfigININew() {
    // Cleanup empty NUMBERS
    for (var j = 0; j < NUMBERS.length; ++j) {
        if ((NUMBERS[j]["digit"].length + NUMBERS[j]["analog"].length) == 0) {
            NUMBERS.splice(j, 1);
        }
    }

    config_split = new Array(0);

    for (var cat in config_param) {
        text = "[" + cat + "]";
		  
        if (!config_category[cat]["enabled"]) {
            text = ";" + text;
        }
        
        config_split.push(text);

        for (var name in config_param[cat]) {
            if (config_param[cat][name]["Numbers"]) {
                for (_num in NUMBERS) {
                    text = NUMBERS[_num]["name"] + "." + name;

                    var text = text + " =" 
                         
                    for (var j = 1; j <= config_param[cat][name]["anzParam"]; ++j) {
                        if (!(typeof NUMBERS[_num][cat][name]["value"+j] == 'undefined')) {
                            text = text + " " + NUMBERS[_num][cat][name]["value"+j];
                        }
                    }
						 
                    if (!NUMBERS[_num][cat][name]["enabled"]) {
                        text = ";" + text;
                    }
						 
                    config_split.push(text);
                }
            }
            else {
                var text = name + " =" 
                    
                for (var j = 1; j <= config_param[cat][name]["anzParam"]; ++j) {
                    if (!(typeof config_param[cat][name]["value"+j] == 'undefined')) {
                        text = text + " " + config_param[cat][name]["value"+j];
                    }
                }
					
                if (!config_param[cat][name]["enabled"]) {
                    text = ";" + text;
                }
					
                config_split.push(text);
            }
        }
		  
        if (cat == "Digits") {
            for (var _roi in NUMBERS) {
                if (NUMBERS[_roi]["digit"].length > 0) {
                    for (var _roiddet in NUMBERS[_roi]["digit"]) {
                        text = NUMBERS[_roi]["name"] + "." + NUMBERS[_roi]["digit"][_roiddet]["name"];
                        text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["x"];
                        text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["y"];
                        text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["dx"];
                        text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["dy"];
                        text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["CCW"];
                        config_split.push(text);
                    }
                }
            }
        }
		  
        if (cat == "Analog") {
            for (var _roi in NUMBERS) {
                if (NUMBERS[_roi]["analog"].length > 0) {
                    for (var _roiddet in NUMBERS[_roi]["analog"]) {
                        text = NUMBERS[_roi]["name"] + "." + NUMBERS[_roi]["analog"][_roiddet]["name"];
                        text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["x"];
                        text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["y"];
                        text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["dx"];
                        text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["dy"];
                        text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["CCW"];
                        config_split.push(text);
                    }
                }
            }
        }
		  
        if (cat == "Alignment") {
            for (var _roi in REFERENCES) {
                text = REFERENCES[_roi]["name"];
                text = text + " " + REFERENCES[_roi]["x"];
                text = text + " " + REFERENCES[_roi]["y"];
                config_split.push(text);
            }
        }

        config_split.push("");
    }
}

function SaveConfigToServer(_domainname){
     // leere Zeilen am Ende löschen
     var zw = config_split.length - 1;
	 
     while (config_split[zw] == "") {
          config_split.pop();
     }

     var config_gesamt = "";
	 
     for (var i = 0; i < config_split.length; ++i)
     {
          config_gesamt = config_gesamt + config_split[i] + "\n";
     } 

     FileDeleteOnServer("/config/config.ini", _domainname);
     FileSendContent(config_gesamt, "/config/config.ini", _domainname);          
}

function GetReferencesInfo(){
    return REFERENCES;
}

function CopyReferenceToImgTmp(_domainname) {
    for (index = 0; index < 2; ++index) {
        _filenamevon = REFERENCES[index]["name"];
        _filenamenach = _filenamevon.replace("/config/", "/img_tmp/");
        FileDeleteOnServer(_filenamenach, _domainname);
        FileCopyOnServer(_filenamevon, _filenamenach, _domainname);
     
        _filenamevon = _filenamevon.replace(".jpg", "_org.jpg");
        _filenamenach = _filenamenach.replace(".jpg", "_org.jpg");
        FileDeleteOnServer(_filenamenach, _domainname);
        FileCopyOnServer(_filenamevon, _filenamenach, _domainname);
    }
}

function UpdateConfigReferences(_domainname){
    for (var index = 0; index < 2; ++index) {
        _filenamenach = REFERENCES[index]["name"];
        _filenamevon = _filenamenach.replace("/config/", "/img_tmp/");
        FileDeleteOnServer(_filenamenach, _domainname);
        FileCopyOnServer(_filenamevon, _filenamenach, _domainname);
     
        _filenamenach = _filenamenach.replace(".jpg", "_org.jpg");
        _filenamevon = _filenamevon.replace(".jpg", "_org.jpg");
        FileDeleteOnServer(_filenamenach, _domainname);
        FileCopyOnServer(_filenamevon, _filenamenach, _domainname);
    }
}

function UpdateConfigReference(_anzneueref, _domainname){
    var index = 0;

    if (_anzneueref == 1) {	
        index = 0;
    }

    else if (_anzneueref == 2) {
        index = 1;
    }

    _filenamenach = REFERENCES[index]["name"];
    _filenamevon = _filenamenach.replace("/config/", "/img_tmp/");

    FileDeleteOnServer(_filenamenach, _domainname);
    FileCopyOnServer(_filenamevon, _filenamenach, _domainname);

    _filenamenach = _filenamenach.replace(".jpg", "_org.jpg");
    _filenamevon = _filenamevon.replace(".jpg", "_org.jpg");

    FileDeleteOnServer(_filenamenach, _domainname);
    FileCopyOnServer(_filenamevon, _filenamenach, _domainname);
}	

function getNUMBERInfo(){
     return NUMBERS;
}

function getNUMBERS(_name, _type, _create = true) {
    _pospunkt = _name.indexOf (".");
    
    if (_pospunkt > -1) {
        _digit = _name.substring(0, _pospunkt);
        _roi = _name.substring(_pospunkt+1);
    }
    else {
        _digit = "default";
        _roi = _name;
    }

    _ret = -1;

    for (i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == _digit) {
            _ret = NUMBERS[i];
        }
    }

    if (!_create) {         // nicht gefunden und soll auch nicht erzeugt werden, ggf. geht eine NULL zurück
          return _ret;
    }

    if (_ret == -1) {
        _ret = new Object();
        _ret["name"] = _digit;
        _ret['digit'] = new Array();
        _ret['analog'] = new Array();

        for (_cat in config_param) {
            for (_param in config_param[_cat]) {
                if (config_param[_cat][_param]["Numbers"] == true){
                    if (typeof  _ret[_cat] == 'undefined') {
                        _ret[_cat] = new Object();
                    }
					
                    _ret[_cat][_param] = new Object();
                    _ret[_cat][_param]["found"] = false;
                    _ret[_cat][_param]["enabled"] = false;
                    _ret[_cat][_param]["anzParam"] = config_param[_cat][_param]["anzParam"]; 
                }
            }
        }

        NUMBERS.push(_ret);
    }

    if (typeof _type == 'undefined') {            // muss schon existieren !!! - also erst nach Digits / Analog aufrufen
        return _ret;
    }

    neuroi = new Object();
    neuroi["name"] = _roi;
    _ret[_type].push(neuroi);

    return neuroi;
}

function CreateNUMBER(_numbernew){
    found = false;
    
    for (i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == _numbernew) {
            found = true;
        }
    }

    if (found) {
        return "Number sequence name is already existing, please choose another name";
    }

    _ret = new Object();
    _ret["name"] = _numbernew;
    _ret['digit'] = new Array();
    _ret['analog'] = new Array();

    for (_cat in config_param) {
        for (_param in config_param[_cat]) {
            if (config_param[_cat][_param]["Numbers"] == true) {
                if (typeof (_ret[_cat]) === "undefined") {
                    _ret[_cat] = new Object();
                }
					
                _ret[_cat][_param] = new Object();
					
                if (config_param[_cat][_param]["defaultValue"] === "") {
                    _ret[_cat][_param]["found"] = false;
                    _ret[_cat][_param]["enabled"] = false;
                }
                else {
                    _ret[_cat][_param]["found"] = true;
                    _ret[_cat][_param]["enabled"] = true;
                    _ret[_cat][_param]["value1"] = config_param[_cat][_param]["defaultValue"];

                }
					
                _ret[_cat][_param]["anzParam"] = config_param[_cat][_param]["anzParam"]; 
            }
        }
    }

    NUMBERS.push(_ret);               
    return "";
}

function DeleteNUMBER(_delete){
    if (NUMBERS.length == 1) {
        return "One number sequence is mandatory. Therefore this cannot be deleted"
    }
     
    index = -1;
	 
    for (i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == _delete) {
            index = i;
        }
    }

    if (index > -1) {
        NUMBERS.splice(index, 1);
    }

    return "";
}

function RenameNUMBER(_alt, _neu){
    if ((_neu.indexOf(".") >= 0) || (_neu.indexOf(",") >= 0) || (_neu.indexOf(" ") >= 0) || (_neu.indexOf("\"") >= 0)) {
        return "Number sequence name must not contain , . \" or a space";
    }

    index = -1;
    found = false;
    
	for (i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == _alt) {
            index = i;
        }
		
        if (NUMBERS[i]["name"] == _neu) {
            found = true;
        }
    }

    if (found) {
        return "Number sequence name is already existing, please choose another name";
    }

    NUMBERS[index]["name"] = _neu;
     
    return "";
}

function getROIInfo(_typeROI, _number){
    index = -1;
    
    for (var i = 0; i < NUMBERS.length; ++i) {
        if (NUMBERS[i]["name"] == _number) {
            index = i;
        }
    }

    if (index != -1) {
        return NUMBERS[index][_typeROI];
    }
    else {
        return "";
    }
}

function CreateROI(_number, _type, _pos, _roinew, _x, _y, _dx, _dy, _CCW){
    _indexnumber = -1;
    
    for (j = 0; j < NUMBERS.length; ++j) {
        if (NUMBERS[j]["name"] == _number) {
            _indexnumber = j;
        }
    }

    if (_indexnumber == -1) {
        return "Number sequence not existing. ROI cannot be created"
    }

    found = false;
    
    for (i = 0; i < NUMBERS[_indexnumber][_type].length; ++i) {
        if (NUMBERS[_indexnumber][_type][i]["name"] == _roinew) {
            found = true;
        }
    }

    if (found) {
        return "ROI name is already existing, please choose another name";
    }

    _ret = new Object();
    _ret["name"] = _roinew;
    _ret["x"] = _x;
    _ret["y"] = _y;
    _ret["dx"] = _dx;
    _ret["dy"] = _dy;
    _ret["ar"] = _dx / _dy;
    _ret["CCW"] = _CCW;

    NUMBERS[_indexnumber][_type].splice(_pos+1, 0, _ret);

    return "";
}

function RenameROI(_number, _type, _alt, _neu){
    if ((_neu.includes("=")) || (_neu.includes(".")) || (_neu.includes(":")) || (_neu.includes(",")) || (_neu.includes(";")) || (_neu.includes(" ")) || (_neu.includes("\""))) {
        return "ROI name must not contain . : , ; = \" or space";
    }

    index = -1;
    found = false;
    _indexnumber = -1;
    
    for (j = 0; j < NUMBERS.length; ++j) {
        if (NUMBERS[j]["name"] == _number) {
            _indexnumber = j;
        }
    }

    if (_indexnumber == -1) {
        return "Number sequence not existing. ROI cannot be renamed"
    }

    for (i = 0; i < NUMBERS[_indexnumber][_type].length; ++i) {
        if (NUMBERS[_indexnumber][_type][i]["name"] == _alt) {
            index = i;
        }
		
        if (NUMBERS[_indexnumber][_type][i]["name"] == _neu) {
            found = true;
        }
    }

    if (found) {
        return "ROI name is already existing, please choose another name";
    }

    NUMBERS[_indexnumber][_type][index]["name"] = _neu;
     
    return "";
}

function ExtractROIs(_aktline, _type){
    var linesplit = split_line(_aktline);
    abc = getNUMBERS(linesplit[0], _type);
    abc["pos_ref"] = _aktline;
    abc["x"] = linesplit[1];
    abc["y"] = linesplit[2];
    abc["dx"] = linesplit[3];
    abc["dy"] = linesplit[4];
    abc["ar"] = parseFloat(linesplit[3]) / parseFloat(linesplit[4]);
    abc["CCW"] = "false";
    
    if (linesplit.length >= 6) {
        abc["CCW"] = linesplit[5];
	}
}
