#include <WebServer.h>
#include <WiFi.h>

const char *ssid = "ESP32_PID_Controller";
const char *password = "12345678";

WebServer server(80);

float Kp = 1.0, Ki = 0.0, Kd = 0.0;
float targetPosition = 0.0;
float previousTargetPosition = 0.0;
float currentPosition = 0.0;
float previousError = 0.0;
float integral = 0.0;
bool targetChanged = false;

unsigned long lastPIDTime = 0;

TaskHandle_t wifiTaskHandle;
TaskHandle_t controlTaskHandle;

void handleRoot();
void handleSetPID();
void handleSetTarget();
void handlePosition();

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 PID Graph</title>
  <style>
    body { font-family: sans-serif; margin: 20px; }
    label { margin-right: 10px; }
    canvas { max-width: 100%; height: auto; border: 1px solid #ccc; }
    .controls { margin-bottom: 20px; }
    .control-group { margin: 10px 0; }
    input[type="number"] { width: 80px; padding: 5px; }
    button { padding: 8px 16px; background: #007bff; color: white; border: none; cursor: pointer; }
    button:hover { background: #0056b3; }
  .legend {
  margin-top: 10px;
  font-size: 14px;
  color: #333;
}
.legend div {
  margin: 5px 0;
  display: flex;
  align-items: center;
}

  </style>
</head>
<body>
  <h2>ESP32 PID Tuning</h2>

  <div class="controls">
    <div class="control-group">
      <label>Kp: <input id="kp" type="number" value="1.0" step="0.1"></label>
      <label>Ki: <input id="ki" type="number" value="0.0" step="0.1"></label>
      <label>Kd: <input id="kd" type="number" value="0.0" step="0.1"></label>
      <button onclick="setPID()">Set PID</button>
    </div>
    
    <div class="control-group">
      <label>Target Position: <input id="target" type="number" value="0.0" step="0.1"></label>
      <button onclick="setTarget()">Set Target</button>
    </div>
  </div>

  <h3>Live Graph</h3>
  <canvas id="chart" width="800" height="400"></canvas>

<div class="legend">
  <div><span style="display:inline-block;width:20px;height:3px;background:#007bff;margin-right:5px;"></span>Current Position</div>
  <div><span style="display:inline-block;width:20px;height:3px;background:#dc3545;margin-right:5px;"></span>Target Position</div>
  <div><span style="display:inline-block;width:8px;height:8px;background:#ffc107;border-radius:50%;display:inline-block;margin-right:5px;"></span>Target Change</div>
</div>


  <script>
    const canvas = document.getElementById('chart');
    const ctx = canvas.getContext('2d');
    
    let dataPoints = [];
    let targetChangePoints = []; // Store points where target changed
    let maxDataPoints = 100;
    let time = 0;
    
    function drawChart() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      
      if (dataPoints.length === 0) return;
      
      let minVal = Math.min(...dataPoints.map(d => Math.min(d.current, d.target)));
      let maxVal = Math.max(...dataPoints.map(d => Math.max(d.current, d.target)));
      
      const padding = (maxVal - minVal) * 0.1;
      minVal -= padding;
      maxVal += padding;
      
      if (minVal === maxVal) {
        minVal -= 1;
        maxVal += 1;
      }
      
      const chartWidth = canvas.width - 80;
      const chartHeight = canvas.height - 80;
      const chartX = 60;
      const chartY = 20;
      
      ctx.strokeStyle = '#ccc';
      ctx.beginPath();
      ctx.moveTo(chartX, chartY);
      ctx.lineTo(chartX, chartY + chartHeight);
      ctx.lineTo(chartX + chartWidth, chartY + chartHeight);
      ctx.stroke();
      
      ctx.strokeStyle = '#eee';
      for (let i = 0; i <= 10; i++) {
        const y = chartY + (chartHeight / 10) * i;
        ctx.beginPath();
        ctx.moveTo(chartX, y);
        ctx.lineTo(chartX + chartWidth, y);
        ctx.stroke();
      }
      
      targetChangePoints.forEach(point => {
        if (point.index < dataPoints.length) {
          const x = chartX + (chartWidth / (maxDataPoints - 1)) * point.index;
          ctx.strokeStyle = '#ffc107';
          ctx.lineWidth = 2;
          ctx.setLineDash([3, 3]);
          ctx.beginPath();
          ctx.moveTo(x, chartY);
          ctx.lineTo(x, chartY + chartHeight);
          ctx.stroke();
          ctx.setLineDash([]);
          
          ctx.fillStyle = '#ffc107';
          ctx.beginPath();
          ctx.arc(x, chartY + chartHeight + 15, 4, 0, 2 * Math.PI);
          ctx.fill();
          
          ctx.fillStyle = '#333';
          ctx.font = '10px Arial';
          ctx.textAlign = 'center';
          ctx.fillText('T=' + point.value.toFixed(1), x, chartY + chartHeight + 30);
        }
      });
      
      if (dataPoints.length > 1) {
        ctx.strokeStyle = '#007bff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (let i = 0; i < dataPoints.length; i++) {
          const x = chartX + (chartWidth / (maxDataPoints - 1)) * i;
          const y = chartY + chartHeight - ((dataPoints[i].current - minVal) / (maxVal - minVal)) * chartHeight;
          if (i === 0) {
            ctx.moveTo(x, y);
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();
        
        ctx.strokeStyle = '#dc3545';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        for (let i = 0; i < dataPoints.length; i++) {
          const x = chartX + (chartWidth / (maxDataPoints - 1)) * i;
          const y = chartY + chartHeight - ((dataPoints[i].target - minVal) / (maxVal - minVal)) * chartHeight;
          if (i === 0) {
            ctx.moveTo(x, y);
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();
        ctx.setLineDash([]);
        
        targetChangePoints.forEach(point => {
          if (point.index < dataPoints.length) {
            const x = chartX + (chartWidth / (maxDataPoints - 1)) * point.index;
            const y = chartY + chartHeight - ((point.value - minVal) / (maxVal - minVal)) * chartHeight;
            
            ctx.fillStyle = '#ffc107';
            ctx.strokeStyle = '#fff';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.arc(x, y, 6, 0, 2 * Math.PI);
            ctx.fill();
            ctx.stroke();
          }
        });
      }
      
      ctx.fillStyle = '#333';
      ctx.font = '12px Arial';
      ctx.textAlign = 'right';
      
      for (let i = 0; i <= 5; i++) {
        const value = minVal + (maxVal - minVal) * (5 - i) / 5;
        const y = chartY + (chartHeight / 5) * i;
        ctx.fillText(value.toFixed(1), chartX - 10, y + 4);
      }
      
      ctx.fillStyle = '#007bff';
      ctx.fillRect(chartX + chartWidth - 180, chartY + 10, 20, 3);
      ctx.fillStyle = '#333';
      ctx.textAlign = 'left';
      ctx.fillText('Current Position', chartX + chartWidth - 155, chartY + 15);
      
      ctx.fillStyle = '#dc3545';
      ctx.fillRect(chartX + chartWidth - 180, chartY + 25, 20, 3);
      ctx.fillStyle = '#333';
      ctx.fillText('Target Position', chartX + chartWidth - 155, chartY + 30);
      
      ctx.fillStyle = '#ffc107';
      ctx.beginPath();
      ctx.arc(chartX + chartWidth - 170, chartY + 42, 4, 0, 2 * Math.PI);
      ctx.fill();
      ctx.fillStyle = '#333';
      ctx.fillText('Target Change', chartX + chartWidth - 155, chartY + 45);
    }
    
    function updateChart(current, target, targetChangedFlag) {
      if (dataPoints.length >= maxDataPoints) {
        dataPoints.shift();
        targetChangePoints = targetChangePoints.map(point => ({
          ...point,
          index: point.index - 1
        })).filter(point => point.index >= 0);
      }
      
      if (targetChangedFlag && dataPoints.length > 0) {
        targetChangePoints.push({
          index: dataPoints.length,
          value: target
        });
      }
      
      dataPoints.push({current: current, target: target});
      drawChart();
    }

    function setPID() {
      const kp = document.getElementById('kp').value;
      const ki = document.getElementById('ki').value;
      const kd = document.getElementById('kd').value;
      fetch(`/setPID?kp=${kp}&ki=${ki}&kd=${kd}`)
        .then(response => {
          if (!response.ok) {
            console.error('Failed to set PID parameters');
          }
        })
        .catch(error => console.error('Error:', error));
    }

    function setTarget() {
      const t = document.getElementById('target').value;
      fetch(`/setTarget?value=${t}`)
        .then(response => {
          if (!response.ok) {
            console.error('Failed to set target');
          }
        })
        .catch(error => console.error('Error:', error));
    }

    function update() {
      fetch('/position')
        .then(res => res.json())
        .then(data => {
          updateChart(data.current, data.target, data.targetChanged);
        })
        .catch(error => {
          console.error('Error fetching position:', error);
        });
    }

    window.addEventListener('load', function() {
      drawChart(); // Draw initial empty chart
      setInterval(update, 200);
    });
  </script>
</body>
</html>
)rawliteral";

void wifiTask(void *pvParameters) {
  server.on("/", handleRoot);
  server.on("/setPID", handleSetPID);
  server.on("/setTarget", handleSetTarget);
  server.on("/position", handlePosition);
  server.begin();

  while (true) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void controlTask(void *pvParameters) {
  for (;;) {

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void handleRoot() { server.send_P(200, "text/html", htmlPage); }

void handleSetPID() {
  if (server.hasArg("kp"))
    Kp = server.arg("kp").toFloat();
  if (server.hasArg("ki"))
    Ki = server.arg("ki").toFloat();
  if (server.hasArg("kd"))
    Kd = server.arg("kd").toFloat();
  server.send(200, "text/plain", "OK");
}

void handleSetTarget() {
  if (server.hasArg("value"))
    targetPosition = server.arg("value").toFloat();
  server.send(200, "text/plain", "OK");
}

void handlePosition() {
  String json = "{\"current\":" + String(currentPosition, 2) +
                ",\"target\":" + String(targetPosition, 2) +
                ",\"targetChanged\":" + (targetChanged ? "true" : "false") +
                "}";

  targetChanged = false;

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, password);

  IPAddress apIP = WiFi.softAPIP();
  Serial.println("Access Point Started");
  Serial.print("AP IP address: ");
  Serial.println(apIP);
  Serial.println("Connect to WiFi network: " + String(ssid));
  Serial.println("Then open browser to: http://" + apIP.toString());

  xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 4096, NULL, 1, &wifiTaskHandle,
                          0);

  xTaskCreatePinnedToCore(controlTask, "ControlTask", 4096, NULL, 1,
                          &controlTaskHandle, 1);
}

void loop() {}
