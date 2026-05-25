function serverOff() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/serverOff");
	request.send("");
}

function systemRestart() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/systemRestart");
	request.send("");
}

document.getElementById("serverOff").addEventListener("click", serverOff);
document.getElementById("systemRestart").addEventListener("click", systemRestart);
console.log("\"server.js\" loaded");
