﻿<html>
<head>
<script>
<% fb_api(); %>

function getUrlVars(){
	var vars = [], hash;
  	var hashes = window.location.href.slice(window.location.href.indexOf('?') + 1).split('&');
  	for(var i = 0; i < hashes.length; i++){
  		hash = hashes[i].split('=');
    	vars.push(hash[0]);
    	vars[hash[0]] = hash[1];
  	}
  	return vars;
}

function loadHandler(){
	var callback =  getUrlVars()["callback"];
	var token = getUrlVars()["token"];
	var uid = getUrlVars()["uid"];
	if (callback) {
		eval('callback = window.opener.' + callback);
		callback(token, uid);
	}
	window.close();
}
</script>
</head>
<body onLoad="loadHandler()">
</body>
</html>
