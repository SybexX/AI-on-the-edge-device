// ============================================================================
// Configuration Management - Server Communication
// ============================================================================

function SaveConfigToServer(domainname) {
    // Remove trailing empty lines
    var zw = config_split.length - 1;

    while (zw >= 0 && config_split[zw] == "") {
        config_split.pop();
        zw--;
    }

    var config = "";

    for (var i = 0; i < config_split.length; ++i) {
        config = config + config_split[i] + "\n";
    }

    FileDeleteOnServer("/config/config.ini", domainname);
    FileSendContent(config, "/config/config.ini", domainname);
}

function UpdateConfig(zw, index, enhance, domainname) {
    var namezw = zw["name"];
    FileCopyOnServer("/img_tmp/ref_zw.jpg", namezw, domainname);
    var namezwOrg = zw["name"].replace(".jpg", "_org.jpg");
    FileCopyOnServer("/img_tmp/ref_zw_org.jpg", namezwOrg, domainname);
}

// ============================================================================
// Image Processing
// ============================================================================

function createReader(file) {
    if (typeof FileReader === 'undefined') {
        console.error("FileReader API not available");
        return;
    }

    var reader = new FileReader();
    var image = new Image();

    reader.onload = function(evt) {
        var loadedImage = new Image();

        loadedImage.onload = function(evt) {
            var width = this.width;
            var height = this.height;
            console.log("Image loaded: " + width + "x" + height);
        };

        loadedImage.onerror = function() {
            console.error("Error loading image");
        };

        loadedImage.src = evt.target.result;
    };

    reader.onerror = function() {
        console.error("Error reading file");
    };

    reader.readAsDataURL(file);
}

// ============================================================================
// Config String Parsing
// ============================================================================

/**
 * Parse a line from config file into tokens
 * Handles multiple formats:
 *  - key = value
 *  - key = value1 value2 value3 ...
 *  - key value1 value2 value3 ...
 *
 * Special handling for password fields which may contain delimiters
 */
function ZerlegeZeile(input, delimiter = " =\t\r") {
    var output = [];

    // Special handling for password and token fields
    if (input.includes("password") || input.includes("Token")) {
        // Line contains sensitive data; use only the first '=' as delimiter
        var pos = input.indexOf("=");
        if (pos > -1) {
            var delim = " \t\r";
            output.push(trim(input.substr(0, pos), delim));
            output.push(trim(input.substr(pos + 1, input.length), delim));
            return output;
        }
    }

    // Standard parsing
    input = trim(input, delimiter);
    var pos = findDelimiterPos(input, delimiter);

    while (pos > -1) {
        var token = input.substr(0, pos);
        token = trim(token, delimiter);

        if (token.length > 0) {
            output.push(token);
        }

        input = input.substr(pos + 1, input.length);
        input = trim(input, delimiter);
        pos = findDelimiterPos(input, delimiter);
    }

    // Add the last token
    if (input.length > 0) {
        output.push(input);
    }

    return output;
}

function findDelimiterPos(input, delimiter) {
    var pos = -1;

    for (var anz = 0; anz < delimiter.length; ++anz) {
        var aktDel = delimiter[anz];
        var zw = input.indexOf(aktDel);

        if (zw > -1) {
            if (pos > -1) {
                if (zw < pos) {
                    pos = zw;
                }
            } else {
                pos = zw;
            }
        }
    }

    return pos;
}

function trim(istring, adddelimiter) {
    // Trim from start
    while ((istring.length > 0) && (adddelimiter.indexOf(istring[0]) >= 0)) {
        istring = istring.substr(1, istring.length - 1);
    }

    // Trim from end
    while ((istring.length > 0) && (adddelimiter.indexOf(istring[istring.length - 1]) >= 0)) {
        istring = istring.substr(0, istring.length - 1);
    }

    return istring;
}

// ============================================================================
// Configuration Loading
// ============================================================================

function getConfig() {
    return config_gesamt;
}

function loadConfig(domainname) {
    var xhttp = new XMLHttpRequest();

    try {
        var url = domainname + '/fileserver/config/config.ini';
        xhttp.open("GET", url, false);
        xhttp.send();

        if (xhttp.status >= 200 && xhttp.status < 300) {
            config_gesamt = xhttp.responseText;
            // Fix typo in config file (historical correction)
            config_gesamt = config_gesamt.replace("InitalRotate", "InitialRotate");
        } else {
            console.warn("Failed to load config:", xhttp.status, xhttp.statusText);
        }
    } catch (error) {
        console.error("Error loading config:", error);
    }

    return true;
}

// ============================================================================
// Data Conversion
// ============================================================================

function dataURLtoBlob(dataurl) {
    var arr = dataurl.split(',');
    var mimeMatch = arr[0].match(/:(.*?);/);
    var mime = mimeMatch ? mimeMatch[1] : 'application/octet-stream';
    var bstr = atob(arr[1]);
    var n = bstr.length;
    var u8arr = new Uint8Array(n);

    while (n--) {
        u8arr[n] = bstr.charCodeAt(n);
    }

    return new Blob([u8arr], { type: mime });
}

// ============================================================================
// Server File Operations
// ============================================================================

function FileCopyOnServer(source, target, domainname = "") {
    var url = domainname + "/editflow?task=copy&in=" + encodeURIComponent(source) + "&out=" + encodeURIComponent(target);
    var xhttp = new XMLHttpRequest();

    try {
        xhttp.open("GET", url, false);
        xhttp.send();

        if (xhttp.status >= 200 && xhttp.status < 300) {
            console.log("File copied: " + source + " -> " + target);
        } else {
            console.warn("Failed to copy file:", xhttp.status, xhttp.statusText);
        }
    } catch (error) {
        console.error("Error copying file:", error);
    }
}

function FileDeleteOnServer(filename, domainname = "") {
    var xhttp = new XMLHttpRequest();
    var okay = false;

    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 200) {
                okay = true;
                console.log("File deleted: " + filename);
            } else if (xhttp.status == 0) {
                console.error("Server closed connection abruptly");
            } else {
                console.error("Error deleting file:", xhttp.status, xhttp.responseText);
            }
        }
    };

    try {
        var url = domainname + "/delete" + filename;
        xhttp.open("POST", url, false);
        xhttp.send();
    } catch (error) {
        console.error("Error deleting file:", error);
    }

    return okay;
}

function FileSendContent(content, filename, domainname = "") {
    var xhttp = new XMLHttpRequest();
    var okay = false;

    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 200) {
                okay = true;
                console.log("File sent: " + filename);
            } else if (xhttp.status == 0) {
                console.error("Server closed connection abruptly");
                if (typeof firework !== 'undefined') {
                    firework.launch('Server closed the connection abruptly!', 'danger', 30000);
                }
            } else {
                console.error("Error sending file:", xhttp.status, xhttp.responseText);
                if (typeof firework !== 'undefined') {
                    firework.launch('An error occurred: ' + xhttp.responseText, 'danger', 30000);
                }
            }
        }
    };

    try {
        var uploadPath = domainname + "/upload" + filename;
        xhttp.open("POST", uploadPath, false);
        xhttp.send(content);
    } catch (error) {
        console.error("Error sending file:", error);
    }

    return okay;
}

// ============================================================================
// Image Manipulation
// ============================================================================

function MakeRefImageZW(zw, enhance, domainname) {
    var filename = zw["name"].replace("/config/", "/img_tmp/");
    var url = domainname + "/editflow?task=cutref&in=/config/reference.jpg&out=" + encodeURIComponent(filename);
    url = url + "&x=" + zw["x"] + "&y=" + zw["y"] + "&dx=" + zw["dx"] + "&dy=" + zw["dy"];

    if (enhance === true) {
        url = url + "&enhance=true";
    }

    var xhttp = new XMLHttpRequest();

    try {
        xhttp.open("GET", url, false);
        xhttp.send();

        if (xhttp.responseText == "CutImage Done") {
            if (enhance === true) {
                if (typeof firework !== 'undefined') {
                    firework.launch('Image Contrast got enhanced', 'success', 5000);
                }
            } else {
                if (typeof firework !== 'undefined') {
                    firework.launch('Alignment Marker have been updated', 'success', 5000);
                }
            }
            return true;
        } else {
            console.error("Image operation failed:", xhttp.responseText);
            return false;
        }
    } catch (error) {
        console.error("Error making reference image:", error);
        return false;
    }
}
