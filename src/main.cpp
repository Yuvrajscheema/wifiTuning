#include <WebServer.h>
#include <WiFi.h>

const char *ssid = "SSID";
const char *password = "PASSWORD";

WebServer server(80);

float kP = 0.0, kD = 0.0, kI = 0.0;

SemaphoreHandle_t pidMutex;

void examplePIDTask(void *arg) {
  for (;;) {
    xSemaphoreTake(pidMutex, portMAX_DELAY);
    ;
    float local_kP = kP;
    float local_kD = kD;
    float local_kI = kI;
    xSemaphoreGive(pidMutex);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 PID Tuner</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f0f0f0;
      margin: 0;
      padding: 20px;
    }
    .container {
      max-width: 500px;
      margin: 0 auto;
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    h1 {
      text-align: center;
      color: #2c3e50;
    }
    .form-group {
      margin-bottom: 15px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-weight: bold;
    }
    input[type="number"] {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 4px;
      box-sizing: border-box;
    }
    input[type="submit"] {
      background: #3498db;
      color: white;
      border: none;
      padding: 12px 20px;
      border-radius: 4px;
      cursor: pointer;
      font-size: 16px;
      width: 100%;
    }
    input[type="submit"]:hover {
      background: #2980b9;
    }
    .status {
      margin-top: 15px;
      padding: 10px;
      background: #eaf7ea;
      border-radius: 4px;
      text-align: center;
      display: none;
    }
    .current-value {
      background: #f9f9f9;
      padding: 15px;
      border-radius: 4px;
      margin-top: 20px;
    }
  </style>
  <script>
  function updateValues() {
    const form = document.getElementById('pidForm');
    document.getElementById('currentKp').innerText = parseFloat(form.kp.value).toFixed(2);
    document.getElementById('currentKi').innerText = parseFloat(form.ki.value).toFixed(2);
    document.getElementById('currentKd').innerText = parseFloat(form.kd.value).toFixed(2);
    }

    
    function showSuccess() {
      const formData = new FormData(document.getElementById('pidForm'));
      document.getElementById('currentKp').innerText = parseFloat(formData.get('kp')).toFixed(2);
      document.getElementById('currentKd').innerText = parseFloat(formData.get('kd')).toFixed(2);
      document.getElementById('currentKi').innerText = parseFloat(formData.get('ki')).toFixed(2);
      const status = document.getElementById('status');
      status.style.display = 'block';
      setTimeout(() => { status.style.display = 'none'; }, 3000);
    }
    
    window.onload = function() {
      updateValues();
      document.getElementById('pidForm').addEventListener('submit', function(e) {
        e.preventDefault();
        const formData = new FormData(this);
        fetch('/set?' + new URLSearchParams({
          kp: formData.get('kp'),
          ki: formData.get('ki'),
          kd: formData.get('kd')
        }))
        .then(() => showSuccess())
        .catch(err => console.error('Error:', err));
      });
    };
  </script>
</head>
<body>
  <div class="container">
    <h1>PID Tuner</h1>
    <form id="pidForm">
      <div class="form-group">
        <label for="kp">Proportional (Kp)</label>
        <input type="number" step="0.01" name="kp" value="%KP%" required>
      </div>
      <div class="form-group">
        <label for="ki">Integral (Ki)</label>
        <input type="number" step="0.01" name="ki" value="%KI%" required>
      </div>
      <div class="form-group">
        <label for="kd">Derivative (Kd)</label>
        <input type="number" step="0.01" name="kd" value="%KD%" required>
      </div>
      <input type="submit" value="Update Parameters">
    </form>
    
    <div id="status" class="status">
      PID parameters updated successfully!
    </div>
    
    <div class="current-value">
      <h3>Current Values</h3>
      <p>Kp: <span id="currentKp">%KP%</span></p>
      <p>Ki: <span id="currentKi">%KI%</span></p>
      <p>Kd: <span id="currentKd">%KD%</span></p>
    </div>
  </div>
</body>
</html>
)rawliteral";

void handleRoot() {
  String page = FPSTR(html);
  page.replace("%KP%", String(kP, 2));
  page.replace("%KD%", String(kD, 2));
  page.replace("%KI%", String(kI, 2));
  server.send(200, "text/html", page);
}

void handleSet() {
  xSemaphoreTake(pidMutex, portMAX_DELAY);
  if (server.hasArg("kp"))
    kP = server.arg("kp").toFloat();
  if (server.hasArg("kd"))
    kD = server.arg("kd").toFloat();
  if (server.hasArg("ki"))
    kI = server.arg("ki").toFloat();
  xSemaphoreGive(pidMutex);
  server.send(200, "text/plain", "OK");
}

void wifiTask(void *arg) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP adress: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
  Serial.println("WebServer started");

  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);

  pidMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(wifiTask, "WiFi Task", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(examplePIDTask, "PID Task", 4096, NULL, 2, NULL, 1);
}

void loop() {}
