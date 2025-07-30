#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>

String machine_state = "OFF";

int xSwitchState = LOW;
int ySwitchState = LOW;

// X STEPPER
AccelStepper stepper_X(1, 2, 4); // initialise accelstepper for a two wire board
int x_speed = 1000;              // speed in steps per second
int x_accel = 500;               // acceleration in steps per second squared
// --- Timing Variables for delays between moves ---
unsigned long lastXMoveFinishedMillis = 0;
// --- Sequence Management Variables ---
int currentXMoveStep = 0; // Tracks which movement in the sequence we are performing

// Y STEPPER
AccelStepper stepper_Y(1, 19, 21); // initialise accelstepper for a two wire board
int y_speed = 1000;                // speed in steps per second
int y_accel = 500;                 // acceleration in steps per second squared
unsigned long lastYMoveFinishedMillis = 0;
int currentYMoveStep = 0; // Tracks which movement in the sequence we are performing

// GENERAL STEPPER VARIABLES
const int stepsPerRevolution = 200; // For a 1.8 degree motor (full steps)
// Adjust for using microstepping, e.g., 200 * 8 for 1/8 microstepping
const long delayBetweenMoves = 3000; // Time in ms to wait after one move finishes before starting the next

// Replace with your network credentials
const char* ssid = "MAKERSPACE";
const char* password = "12345678";

String message = "";
// Allocate the JSON document
JsonDocument doc;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variables to save values from HTML form
bool newRequest = false;

JsonArray x_vals;
JsonArray y_vals;

int next_x = 0;
int next_y = 0;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>Circle selector</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <link rel="icon" href="data:," />
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
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <link rel="icon" href="data:," />
  </head>
  <body>
    <div class="content">
      <button
        onclick="calibrate()"
        style="
          border: 2px solid rgb(255, 111, 0);
          background: rgba(255, 111, 0, 0.6);
          padding: 10px;
          margin: 10px;
          cursor: pointer;
          border-radius: 4px;
          display: block;
        "
      >
        Calibrate to (0,0)
      </button>
      <br />
      <form id="pointsForm" accept-charset="utf-8">
        <label for="cx">Center (x)</label>
        <input
          type="text"
          id="cx"
          name="cx"
          value="0.5"
          onchange="myDrawing.updatePoints()"
        />
        <label for="cy">Center (y)</label>
        <input
          type="text"
          id="cy"
          name="cy"
          value="0.5"
          onchange="myDrawing.updatePoints()"
        />
        <label for="c_rad">Radius</label>
        <input
          type="text"
          id="c_rad"
          name="c_rad"
          value="100"
          onchange="myDrawing.updatePoints()"
        />
        <label for="n_steps">Steps</label>
        <input
          type="text"
          id="n_steps"
          name="n_steps"
          value="10"
          onchange="myDrawing.updatePoints()"
        />

        <button type="button" onclick="formatSend();">Draw</button>
      </form>
      <div style="display: flex; justify-content: center; align-items: center">
        <div
          id="pointsSvg"
          style="height: 100vh; width: 100vh; text-align: center"
        ></div>
      </div>
    </div>
    <script>
      var gateway = `ws://${window.location.hostname}/ws`;
      var websocket;
      window.addEventListener("load", onLoad);
      function initWebSocket() {
        console.log("Trying to open a WebSocket connection...");
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage; // <-- add this line
      }
      function onOpen(event) {
        console.log("Connection opened");
      }
      function onClose(event) {
        console.log("Connection closed");
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
      function calibrate() {
        websocket.send(
          JSON.stringify({ type: "calibrate", xy: { x: 0, y: 0 } })
        );
      }

      function generateCircleXY() {
        let form = document.getElementById("pointsForm");
        let formData = new FormData(form);
        let data = {};
        formData.forEach((value, key) => {
          data[key] = value;
        });

        let theta_steps = Array.from(
          { length: +data.n_steps },
          (v, i) => ((2 * Math.PI) / +data.n_steps) * i
        );
        let x_vals = theta_steps.map(
          (x) => +data.c_rad * Math.cos(x) + 550 * +data.cx
        );
        let y_vals = theta_steps.map(
          (x) => +data.c_rad * Math.sin(x) + 550 * +data.cy
        );
        x_vals.forEach(function (x, i) {
          x_vals[i] = parseFloat(x.toFixed(3));
        });
        y_vals.forEach(function (x, i) {
          y_vals[i] = parseFloat(x.toFixed(3));
        });
        return { x: x_vals, y: y_vals };
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

          vis.margin = { top: 0, right: 0, bottom: 0, left: 0 };

          vis.width = parentWidth - vis.margin.left - vis.margin.right;
          vis.height = parentHeight - vis.margin.top - vis.margin.bottom;

          // SVG drawing area
          vis.svg = d3
            .select(`#${vis.parentElement}`)
            .append("svg")
            .attr("width", vis.width + vis.margin.left + vis.margin.right)
            .attr("height", vis.height + vis.margin.top + vis.margin.bottom)
            .append("g")
            .attr(
              "transform",
              "translate(" + vis.margin.left + "," + vis.margin.top + ")"
            );

          vis.drawingPoints();
        }

        updatePoints() {
          let vis = this;

          let { x, y } = generateCircleXY();
          let newPoints = x.map((x, i) => ({ x: x, y: y[i] }));

          vis.coords = newPoints;

          vis.drawingPoints();
        }

        drawingPoints() {
          let vis = this;

          vis.xyPoints = vis.svg.selectAll(".points").data(vis.coords);

          vis.xyPoints.exit().remove();

          vis.xyPoints
            .enter()
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

      let init_form = document.getElementById("pointsForm");
      let init_formData = new FormData(init_form);
      let init_data = {};
      init_formData.forEach((value, key) => {
        init_data[key] = value;
      });

      let { x, y } = generateCircleXY(init_data);
      let drawing_xy = x.map((x, i) => ({ x: x, y: y[i] }));
      console.log(drawing_xy);
      let myDrawing = new drawPoints("pointsSvg", drawing_xy);
    </script>
  </body>
</html>
)rawliteral";

// Initialize WiFi
void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {

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

  stepper_X.setMaxSpeed(x_speed);
  stepper_X.setAcceleration(x_accel);
  stepper_X.setSpeed(x_speed);
  
  stepper_Y.setSpeed(y_speed);
  stepper_Y.setMaxSpeed(y_speed);
  stepper_Y.setAcceleration(y_accel);

  // Start the very first movement immediately
  Serial.println("Starting 2 revolutions clockwise (initial move)...");
  stepper.moveTo(stepsPerRevolution * 2);
  lastXMoveFinishedMillis = millis(); // Initialize to ensure the first wait period is correct
  currentXMoveStep = 0;               // Set to indicate the first move is in progress
  lastYMoveFinishedMillis = millis(); // Initialize to ensure the first wait period is correct
  currentYMoveStep = 0;               // Set to indicate the first move is in progress
}



void runXStepperForCalibration()
{
  stepper_X.runSpeed();
}
void runYStepperForCalibration()
{
  stepper_Y.runSpeed();
}

void calibration()
{
  xSwitchState = digitalRead(xLimitSwitchPin);
  ySwitchState = digitalRead(yLimitSwitchPin);

  if (reset_x == true)
  {
    if (xSwitchState == LOW)
    {
      runXStepperForCalibration();
    }
    else if (xSwitchState == HIGH)
    {
      Serial.println("STOP THE STEPPER MOTOR");
      stepper_X.stop();
      Serial.println("now X COORDINATE is 0");
      x_stepper_coord = 0;
      stepper_X.setCurrentPosition(0);
      reset_x = false;
      if (reset_y == false)
      {
        requestCalibrate = false;
        ws.cleanupClients();
      }
    }
  }

  if (reset_y == true)
  {
    if (ySwitchState == LOW)
    {
      runYStepperForCalibration();
    }
    else
    {
      digitalWrite(yLed, HIGH);
      Serial.println("STOP THE STEPPER MOTOR");
      Serial.println("now Y COORDINATE is 0");
      y_stepper_coord = 0;
      stepper_Y.setCurrentPosition(0);
      reset_y = false;
      if (reset_x == false)
      {
        requestCalibrate = false;
        ws.cleanupClients();
      }
    }
  }
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

void loop()
{
  switch (machine_state)
  {
  case "CALIBRATE":
    calibration();
    break;
  case "ICE":
    /* code */
    break;
  case "OFF":
  default:
    break;
  }
}

void loop() {
  if (newRequest) {
    icing();
  }
}