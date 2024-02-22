function actualizeData() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4 && this.status == 200) {
            console.log(this.responseText);
            var obj = JSON.parse(this.responseText);
            document.getElementById("actual").innerText = parseFloat(obj.actualDB).toFixed(2);
        }
    };
    xhttp.open("GET", "getActualDB", true);
    xhttp.send();
}

function getMAC() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4 && this.status == 200) {
            console.log(this.responseText);
            var obj = JSON.parse(this.responseText);
            document.getElementById("MAC").innerText = obj.MAC;
        }
    };
    xhttp.open("GET", "getMAC", true);
    xhttp.send();
}

function getLast() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4 && this.status == 200) {
            console.log(this.responseText);
            var obj = JSON.parse(this.responseText);
            document.getElementById("last").innerText = obj.last;
        }
    };
    xhttp.open("GET", "getLast", true);
    xhttp.send();
}

function getTotal() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4 && this.status == 200) {
            console.log(this.responseText);
            var obj = JSON.parse(this.responseText);
            document.getElementById("totaly").innerText = obj.total;
        }
    };
    xhttp.open("GET", "getTotal", true);
    xhttp.send();
}

function setSettings() {
    var xhttp = new XMLHttpRequest();
    xhttp.open("POST", "setSettings", true);
    xhttp.setRequestHeader('Content-Type', 'application/json');
    xhttp.send(JSON.stringify({
        treshold: document.getElementById("treshold").value,
        MAC: document.getElementById("generator").value,
    }));
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4) {
            var response = JSON.parse(this.responseText);
            console.log(response);
            if (response.status == 200) {
                alert('Settings uploaded');
            } else {
                alert('Settings are not uploaded');
            }
        }
    }
}

function getSettings() {
    var xhttp = new XMLHttpRequest();
    xhttp.open("GET", "getSettings", true);
    xhttp.send();
    xhttp.onreadystatechange = function () {
        if (this.readyState == 4) {
            var settings = JSON.parse(this.responseText);
            console.log(settings);
            document.getElementById("treshold").setAttribute('value', (settings.hasOwnProperty('treshold') > 0 ? settings.treshold : ""));
            document.getElementById("generator").setAttribute('value', (settings.hasOwnProperty('MAC') > 0 ? settings.MAC : ""));
        }
    }
}
getSettings();
getMAC()
getLast()
getTotal()
setInterval(actualizeData, 500);