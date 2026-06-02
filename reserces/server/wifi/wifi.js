let wifiList = document.getElementById("wifiList");
let apSsid = document.getElementById("apSsid");
let apPassword = document.getElementById("apPassword");
let wifiSsid = document.getElementById("wifiSsid");
let wifiPassword = document.getElementById("wifiPassword");

function apStart() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/apStart");
	if (apSsid.value == "" || apPassword.value.length < 8) {
		//不符合要求
		request.send();
	}
	{
		//符合要求
		request.send(apSsid.value + '\n' + apPassword.value);
	}
}

function apStop() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/apStop");
	request.send();
}

function wifiConnect() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/wifiStart");
	if (wifiSsid.value == "" || wifiPassword.value.length < 8) {
		//不符合要求
		request.send();
	}
	{
		//符合要求
		request.send(wifiSsid.value + '\n' + wifiPassword.value);
	}
}

function wifiStop() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/wifiStop");
	request.send();
}

function wifiListLoad(list) {
	wifiList.innerHTML = "";

	var entry = "";
	//var rssi = 0;
	var ssid = "";
	for (i = 0; i < list.length; i++) {
		if (list[i] == "") break;
		//rssi = list[i].split(':')[0];
		ssid = list[i].split(':')[1];
		entry = "<button onclick='wifiSsid.value=\"" + ssid + "\"'>" + list[i] + "</button><br/>"
		wifiList.innerHTML += entry;
	}
}

function wifiScan() {
	wifiList.innerHTML = "";
	var request = new XMLHttpRequest();
	request.open("POST", "/api/wifiScan");
	request.onload = function () { wifiListLoad(this.responseText.split('\n')); };
	request.send();
}

document.getElementById("apStart").addEventListener("click", apStart);
document.getElementById("apStop").addEventListener("click", apStop);
document.getElementById("wifiConnect").addEventListener("click", wifiConnect);
document.getElementById("wifiStop").addEventListener("click", wifiStop);
document.getElementById("wifiScan").addEventListener("click", wifiScan);

console.log("\"wifi.js\" loaded");
