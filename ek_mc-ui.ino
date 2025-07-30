#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>

AccelStepper stepper_X(1, 2, 4); // initialise accelstepper for a two wire board
AccelStepper stepper_Y(1, 19, 21); // initialise accelstepper for a two wire board

// Replace with your network credentials
const char* ssid = "MAKERSPACE";
const char* password = "12345678";

String message = "";
// Allocate the JSON document
JsonDocument doc;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

//Variables to save values from HTML form
String steps_X;
String steps_Y;

JsonArray x_vals; 
JsonArray y_vals; 
int coords_pos = 0;
int next_x = 0;
int next_y = 0;

int penDown = 0; 
bool newRequest = false;


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Circle selector</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  body {
    margin: 0;
  }
  .content {
    padding: 30px;
    max-width: 90vw;
    margin: 0 auto;
  }
  </style>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  </head>
  <body>
  <div class="content">


    <form id="pointsForm" accept-charset="utf-8">
        <label for="cx">Center (x)</label>
        <input type="text" id="cx" name="cx" value="0.5" onchange="myDrawing.updatePoints()">
        <label for="cy">Center (y)</label>
        <input type="text" id="cy" name="cy" value="0.5" onchange="myDrawing.updatePoints()">
        <label for="c_rad">Radius</label>
        <input type="text" id="c_rad" name="c_rad" value="100" onchange="myDrawing.updatePoints()">
        <label for="n_steps">Steps</label>
        <input type="text" id="n_steps" name="n_steps" value="10" onchange="myDrawing.updatePoints()">
        
        <button type="button" onclick="formatSend();">Draw</button>
    </form>

    <div style="display: flex; justify-content: center; align-items: center;">
        <div id="pointsSvg" style="height: 100vh; width: 100vh; text-align: center">

        </div>
    </div>

    
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }
  function onOpen(event) {
    console.log('Connection opened');
  }
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  function onMessage(event) {
    console.log(event.data);
  }

  function onLoad(event) {
    initWebSocket();
  }
    function formatSend() {
        let coords = generateCircleXY();
        //let drawing_xy = x.map((x, i) => ({x: x, y: y[i]}));
        console.log(JSON.stringify(coords));
        websocket.send(JSON.stringify(coords));
    }

    function generateCircleXY() {
        let form = document.getElementById('pointsForm');
        let formData = new FormData(form);
        let data = {};
        formData.forEach((value, key) => {
            data[key] = value;
        });

        let theta_steps = Array.from({ length: +data.n_steps}, (v, i) => 2 * Math.PI / ((+data.n_steps)) * i);
        let x_vals = theta_steps.map((x) => +data.c_rad * Math.cos(x) + 550 * +data.cx );
        let y_vals = theta_steps.map((x) => +data.c_rad * Math.sin(x) + 550 * +data.cy );
        x_vals.forEach(function(x, i){
            x_vals[i] = parseFloat(x.toFixed(3));
        });
        y_vals.forEach(function(x, i){
            y_vals[i] = parseFloat(x.toFixed(3));
        });
        return {x: x_vals, y: y_vals};
        
    }
</script>
<!-- d3 -->
<script src="https://d3js.org/d3.v7.min.js"></script>
<script src="https://d3js.org/d3-array.v2.min.js"></script>

<script>
    class drawPoints {
        constructor(parentElement, coords) {
            this.parentElement = parentElement;
            this.coords = coords;

            this.initVis();
        }

        initVis() {
            let vis = this; 

            // Margin convention
            let parentWidth = 550;
            let parentHeight = 550;
            vis.pointSize = 2;

            vis.margin = { top: 0, right: 0, bottom: 0, left: 0};

            vis.width = parentWidth - vis.margin.left - vis.margin.right;
            vis.height = parentHeight - vis.margin.top - vis.margin.bottom;

            // SVG drawing area
            vis.svg = d3.select(`#${vis.parentElement}`).append("svg")
                .attr("width", vis.width + vis.margin.left + vis.margin.right)
                .attr("height", vis.height + vis.margin.top + vis.margin.bottom)
                .append("g")
                .attr("transform", "translate(" + vis.margin.left + "," + vis.margin.top + ")");

            vis.drawingPoints();
        }

        updatePoints() {
            let vis = this;

            let {x, y} = generateCircleXY();
            let newPoints = x.map((x, i) => ({x: x, y: y[i]}));

            vis.coords = newPoints;

            vis.drawingPoints();
        }

        drawingPoints() {
            let vis = this;

            vis.xyPoints = vis.svg.selectAll(".points")
                .data(vis.coords);

            vis.xyPoints.exit().remove();

            vis.xyPoints.enter()
                .append("circle")
                .attr("class", "points")
                .attr("fill", "black")
                .attr("r", vis.pointSize)
                .attr("stroke-width", 1)
                .attr("stroke", "black")
                .merge(vis.xyPoints)
                .attr("cx", (d) => d.x)
                .attr("cy", (d) => d.y);

        }
    }

    
    let init_form = document.getElementById('pointsForm');
    let init_formData = new FormData(init_form);
    let init_data = {};
    init_formData.forEach((value, key) => {
        init_data[key] = value;
    });
    
    let {x, y} = generateCircleXY(init_data);
    let drawing_xy = x.map((x, i) => ({x: x, y: y[i]}));
    console.log(drawing_xy);
    let myDrawing = new drawPoints("pointsSvg", drawing_xy);
    
    
</script>
</body>
</html>
)rawliteral";

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

// Having trouble receiving larger messages with this. it receives but doesn't satify if statement for ? reason
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  Serial.println("received");
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    Serial.println("here");
    data[len] = 0;
    message = (char*)data;
    Serial.println(message);

    // this is from ArduinoJson docs (https://arduinojson.org/v7/example/parser/)
    const char* json = (char*) data;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, json);
    // Test if parsing succeeds.
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    
    x_vals = doc["x"];
    y_vals = doc["y"];

    newRequest = true;
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      //Notify client of motor current state when it first connects
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var) {
  Serial.println("PROCESSOR");
  Serial.println(var);
  if (var == "STATE") {
    return "OFF";
  }
  return String();
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  initWiFi();
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  server.begin();

  stepper_X.setMaxSpeed(1000.0);
  stepper_X.setAcceleration(1000.0);

  stepper_Y.setMaxSpeed(1000.0);
  stepper_Y.setAcceleration(1000.0);
}

void icing() {
  stepper_X.run();
  stepper_Y.run();

  if (stepper_X.distanceToGo() == 0 && stepper_Y.distanceToGo() == 0) {
    if (x_vals.size() == 0 && y_vals.size() == 0) {
      newRequest = false;
      ws.cleanupClients();
      return;
    }
    if (x_vals.size() > 0) {
      next_x = x_vals[0];
      x_vals.remove(0);
      stepper_X.moveTo(next_x.toInt());
    }
    if (y_vals.size() > 0) {
      next_y = y_vals[0];
      y_vals.remove(0);
      stepper_Y.moveTo(next_y.toInt());
    }
  }
}

void loop() {
  if (newRequest) {
    icing();
  }

}
