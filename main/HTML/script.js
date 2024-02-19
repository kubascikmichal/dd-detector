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

setInterval(actualizeData, 1000);