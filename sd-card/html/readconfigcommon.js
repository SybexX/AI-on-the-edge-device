function isCommented(input) {
    let isComment = false;
		  
    if (input.charAt(0) == ';') {
        isComment = true;
        input = input.substr(1, input.length-1);
    }
		  
    return [isComment, input];
} 

function createReader(file) {
     var image = new Image();
	 
     reader.onload = function(evt) {
         var image = new Image();
		 
         image.onload = function(evt) {
             var width = this.width;
             var height = this.height;
         };
		 
         image.src = evt.target.result; 
     };
	 
     reader.readAsDataURL(file);
 }
 
function split_line(input, delimiter = " =\t\r") {
    var Output = Array(0);
	var upper_input = input.toUpperCase();

    if (upper_input.includes("PASSWORD") || upper_input.includes("EAPID") || upper_input.includes("TOKEN") || upper_input.includes("APIKEY") || upper_input.includes("HTTP_PASSWORD")) {
        var pos = input.indexOf("=");
        delimiter = " \t\r";
        Output.push(trim(input.substr(0, pos), delimiter));
		
		var is_pw_encrypted = input.substr(pos + 2, 6);
		
		if (is_pw_encrypted == "**##**") {
			Output.push(encryptDecrypt(input.substr(pos + 8, input.length)));
		}
		else {
			Output.push(trim(input.substr(pos + 1, input.length), delimiter));
		}
    }
    else { 
        input = trim(input, delimiter);
        var pos = findDelimiterPos(input, delimiter);
        var token;
        
		while (pos > -1) {
            token = input.substr(0, pos);
            token = trim(token, delimiter);
            Output.push(token);
			
            input = input.substr(pos + 1, input.length);
            input = trim(input, delimiter);
            pos = findDelimiterPos(input, delimiter);
        }
        
		Output.push(input);
    }
     
    return Output;
}

function findDelimiterPos(input, delimiter) {
    var pos = -1;
    var input_temp;
    var akt_del;
     
    for (var anz = 0; anz < delimiter.length; ++anz) {
        akt_del = delimiter[anz];
        input_temp = input.indexOf(akt_del);
        
		if (input_temp > -1) {
            if (pos > -1) {
                if (input_temp < pos) {
                    pos = input_temp;
				}
            }
            else {
                pos = input_temp;
			}
        }
    }
    
	return pos;
}

function trim(istring, adddelimiter) {
    while ((istring.length > 0) && (adddelimiter.indexOf(istring[0]) >= 0)) {
        istring = istring.substr(1, istring.length-1);
    }
          
    while ((istring.length > 0) && (adddelimiter.indexOf(istring[istring.length-1]) >= 0)) {
        istring = istring.substr(0, istring.length-1);
    }

    return istring;
}

function dataURLtoBlob(dataurl) {
    var arr = dataurl.split(','), mime = arr[0].match(/:(.*?);/)[1],
          bstr = atob(arr[1]), n = bstr.length, u8arr = new Uint8Array(n);
    
	while(n--){
        u8arr[n] = bstr.charCodeAt(n);
    }
    
	return new Blob([u8arr], {type:mime});
}	

function FileCopyOnServer(_source, _target, _domainname = "") {
    url = _domainname + "/editflow?task=copy&in=" + _source + "&out=" + _target;
    var xhttp = new XMLHttpRequest();  
    
	try {
        xhttp.open("GET", url, false);
        xhttp.send();
	} catch (error) {}
}

function FileDeleteOnServer(_filename, _domainname = "") {
    var xhttp = new XMLHttpRequest();
    var okay = false;

    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 200) {
                okay = true;
            } 
			else if (xhttp.status == 0) {
				// firework.launch('Server closed the connection abruptly!', 'danger', 30000);
				// location.reload()
            } 
			else {
				// firework.launch('An error occured: ' + xhttp.responseText, 'danger', 30000);
				// location.reload()
            }
        }
    };
     
	try {
        var url = _domainname + "/delete" + _filename;
        xhttp.open("POST", url, false);
        xhttp.send();
    } catch (error) {}

    return okay;
}

function FileSendContent(_content, _filename, _domainname = "") {
    var xhttp = new XMLHttpRequest();  
    var okay = false;

    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 200) {
                okay = true;
            } 
			else if (xhttp.status == 0) {
				firework.launch('Server closed the connection abruptly!', 'danger', 30000);
            } 
			else {
				firework.launch('An error occured: ' + xhttp.responseText, 'danger', 30000);
            }
        }
    };

    try {
        upload_path = _domainname + "/upload" + _filename;
        xhttp.open("POST", upload_path, false);
        xhttp.send(_content);
    } catch (error) {}
	
    return okay;        
}

// Encrypt password
function EncryptPwString(pwToEncrypt) {
	var _pw_temp = "**##**";
	var pw_temp = "";

	if (isInString(pwToEncrypt, _pw_temp)) {
		pw_temp = pwToEncrypt;
	}
	else {
		pw_temp = _pw_temp + encryptDecrypt(pwToEncrypt);
	}

	return pw_temp;
}

// Decrypt password
function DecryptPwString(pwToDencrypt) {
	var _pw_temp = "**##**";
	var pw_temp = "";
	
    if (isInString(pwToDencrypt, _pw_temp))
    {
        var _temp = ReplaceString(pwToDencrypt, _pw_temp, "");
        pw_temp = encryptDecrypt(_temp);
    }
    else
    {
        pw_temp = pwToDencrypt;
    }

    return pw_temp;
}

function decryptConfigPwOnSD(_domainname = getDomainname()) {
	var _status = "";
	
    var url = _domainname + "/edit_flow?task=pw_decrypt&config_decrypt=true";
    var xhttp = new XMLHttpRequest();  
    
    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 0 || (xhttp.status >= 200 && xhttp.status < 300)) {
                _status = xhttp.responseText;
            }
        }
    };
	
	try {
        xhttp.open("GET", url, false);
        xhttp.send();
	} catch (error) { console.log(error); }
	
	if (_status == "decrypted") {
		return true;
	}
	else {
        return false;
    }
}

function decryptWifiPwOnSD(_domainname = getDomainname()) {
	var _status = "";
	
    var url = _domainname + "/edit_flow?task=pw_decrypt&wifi_decrypt=true";
    var xhttp = new XMLHttpRequest();  

    xhttp.onreadystatechange = function() {
        if (xhttp.readyState == 4) {
            if (xhttp.status == 0 || (xhttp.status >= 200 && xhttp.status < 300)) {
                _status = xhttp.responseText;
            }
        }
    };

	try {
        xhttp.open("GET", url, false);
        xhttp.send();
	} catch (error) { console.log(error); }
	
	if (_status == "decrypted") {
		return true;
	}
	else {
        return false;
    }
}

function encryptDecrypt(input) {
	var key = ['K', 'C', 'Q']; //Can be any chars, and any size array
	var output = [];
	
	for (var i = 0; i < input.length; i++) {
		var charCode = input.charCodeAt(i) ^ key[i % key.length].charCodeAt(0);
		output.push(String.fromCharCode(charCode));
	}

	return output.join("");
}
