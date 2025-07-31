#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>
#include <ESPmDNS.h>

enum machine_state {
  OFF,
  ICE,
  CALIBRATE
};
machine_state currentState = OFF;

int xSwitchState = LOW;
int ySwitchState = LOW;

int xLimitSwitchPin = 3;
int yLimitSwitchPin = 5;

bool reset_x = false;
bool reset_y = false;

int stepXPin = 26;
int dirXPin = 25;
int stepYPin = 18;
int dirYPin = 19;

// Adjust for steppers moving differently
float y_adjustment = 1.4545;
// extruding control?
int array_length;
bool extruding;

// X STEPPER
AccelStepper stepper_X(AccelStepper::DRIVER, stepXPin, dirXPin); // initialise accelstepper for a two wire board
int x_speed = 1000;              // speed in steps per second
int x_accel = 500;               // acceleration in steps per second squared
// --- Timing Variables for delays between moves ---
unsigned long lastXMoveFinishedMillis = 0;
// --- Sequence Management Variables ---
int currentXMoveStep = 0; // Tracks which movement in the sequence we are performing

// Y STEPPER
AccelStepper stepper_Y(AccelStepper::DRIVER, stepYPin, dirYPin); // initialise accelstepper for a two wire board
int y_speed = 1000;                // speed in steps per second
int y_accel = 500;                 // acceleration in steps per second squared
unsigned long lastYMoveFinishedMillis = 0;
int currentYMoveStep = 0; // Tracks which movement in the sequence we are performing

// GENERAL STEPPER VARIABLES
const int stepsPerRevolution = 200; // For a 1.8 degree motor (full steps)
// Adjust for using microstepping, e.g., 200 * 8 for 1/8 microstepping
const long delayBetweenMoves = 3000; // Time in ms to wait after one move finishes before starting the next

// Replace with your network credentials
const char *ssid = "MAKERSPACE";
const char *password = "12345678";

String message = "";
// Allocate the JSON document
JsonDocument doc;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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
      <button type="button" onclick="lineX();">Draw X line</button>
      <button type="button" onclick="lineY();">Draw Y line</button>
      <button type="button" onclick="someShape();">Draw some shape</button>
      <button type="button" onclick="tempCalibrate();">Temporary "calibrate"</button>
      <div style="display: flex; align-items: center">
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
        // let drawing_xy = x.map((x, i) => ({x: x, y: y[i]}));
        console.log(JSON.stringify({mode:"ice", xy: coords}));
        websocket.send(JSON.stringify({mode:"ice", xy: coords}));
      }
      function lineX() {
        let coords = {x: [0, 10, 20, 30, 40], y: [0, 0, 0, 0, 0]}
        console.log(JSON.stringify({mode:"ice", xy: coords}));
        websocket.send(JSON.stringify({mode:"ice", xy: coords}));
      }
      function lineY() {
        let coords = {x: [0, 0, 0, 0, 0], y:[0, 10, 20, 30, 40]}
        console.log(JSON.stringify({mode:"ice", xy: coords}));
        websocket.send(JSON.stringify({mode:"ice", xy: coords}));
      }
      function someShape() {
        let coords = {x: [502, 507, 513, 519, 524, 529, 534, 539, 544, 551, 549, 538, 527, 518, 515, 512, 510, 519, 530, 541, 552, 562, 573, 584, 589, 587, 581, 573, 562, 552, 541, 547, 558, 568, 576, 580, 580, 572, 561, 554, 548, 543, 538, 533, 528, 523, 520, 511, 613, 603, 593, 587, 588, 594, 592, 595, 603, 613, 624, 633, 637, 636, 631, 625, 620, 619, 624, 633, 644, 654, 660, 656, 653, 654, 645, 634, 626, 624, 628, 635, 642, 646, 646, 640, 632, 622, 662, 659, 666, 673, 680, 687, 694, 701, 695, 684, 674, 668, 673, 680, 691, 702, 713, 709, 702, 695, 688, 681, 674, 668, 715, 707, 705, 706, 710, 715, 722, 730, 740, 751, 755, 755, 752, 748, 742, 734, 725, 728, 736, 742, 746, 748, 747, 740, 729, 722, 717, 714, 712, 715], y:[322, 317, 308, 299, 289, 279, 270, 260, 250, 242, 240, 241, 243, 247, 257, 256, 245, 239, 238, 238, 238, 238, 238, 242, 251, 262, 271, 278, 282, 284, 284, 281, 280, 276, 269, 259, 248, 241, 241, 248, 258, 267, 277, 287, 296, 306, 317, 318, 325, 323, 318, 309, 298, 289, 299, 309, 316, 320, 320, 314, 304, 293, 283, 274, 264, 254, 244, 238, 235, 238, 246, 256, 256, 245, 239, 239, 246, 256, 266, 274, 283, 293, 304, 313, 320, 324, 319, 315, 307, 298, 290, 282, 273, 264, 260, 260, 263, 271, 261, 254, 254, 254, 254, 261, 269, 278, 286, 295, 303, 312, 320, 314, 304, 293, 282, 273, 264, 257, 253, 256, 266, 276, 287, 297, 306, 313, 319, 315, 308, 299, 288, 278, 267, 259, 261, 269, 279, 289, 300, 311]};
        console.log(JSON.stringify({mode:"ice", xy: coords}));
        websocket.send(JSON.stringify({mode:"ice", xy: coords}));
      }
      function tempCalibrate() {
        let coords = {x: [0], y: [0]}
        console.log(JSON.stringify({mode:"ice", xy: coords}));
        websocket.send(JSON.stringify({mode:"ice", xy: coords}));
      }
      
      function calibrate() {
        websocket.send(
          JSON.stringify({ mode: "calibrate", xy: { x: 0, y: 0 } })
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
          (x) => +data.c_rad * Math.cos(x) + 550 * 2.089 * +data.cx
        );
        let y_vals = theta_steps.map(
          (x) => +data.c_rad * Math.sin(x) + 550 * +data.cy
        );
        x_vals.forEach(function (x, i) {
          x_vals[i] = parseInt(x);
        });
        y_vals.forEach(function (x, i) {
          y_vals[i] = parseInt(x);
        });
        x_vals.push(x_vals[0], 550 * 2.089 * +data.cx);
        y_vals.push(y_vals[0], 550 * +data.cx);
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
          let parentWidth = 550 * 2.089;
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
  Serial.println("initWiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {

    Serial.println('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
    // Start mDNS
  if (!MDNS.begin("mrfrosty"))
  {
    Serial.println("Error setting up MDNS responder!");
  }
}

// Having trouble receiving larger messages with this. it receives but doesn't satify if statement for ? reason
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  Serial.println("handleWSM");
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
    String mode_string = doc["mode"].as<String>();
    Serial.println(mode_string);
    if (mode_string == "ice") {
      currentState = ICE;
    } else if (mode_string == "calibrate") {
      currentState = CALIBRATE;
      reset_x = true;
      reset_y = true;
    } else {
      currentState = OFF;
    }
    
    x_vals = doc["xy"]["x"];
    Serial.println("X values received:");
    for (JsonVariant v : x_vals) {
      Serial.println(v.as<String>());
    }
    y_vals = doc["xy"]["y"];
    Serial.println("Y values received:");
    for (JsonVariant v : y_vals) {
      Serial.println(v.as<String>());
    }

    array_length = x_vals.size();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  Serial.println("onEvent");
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
  Serial.println("initWS");
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
}

void runXStepperForCalibration()
{
  Serial.println("runX");
  stepper_X.runSpeed();
}
void runYStepperForCalibration()
{
  Serial.println("runY");
  stepper_Y.runSpeed();
}

void calibration()
{
  Serial.println("lets calibrate");
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
      stepper_X.setCurrentPosition(0);
      reset_x = false;
      if (reset_y == false)
      {
        currentState = OFF;
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
      Serial.println("STOP THE STEPPER MOTOR");
      stepper_Y.stop();
      Serial.println("now Y COORDINATE is 0");
      stepper_Y.setCurrentPosition(0);
      reset_y = false;
      if (reset_x == false)
      {
        currentState = OFF;
        ws.cleanupClients();
      }
    }
  }
}

void icing() {
  Serial.println("lets ice");
  stepper_X.run();
  stepper_Y.run();

  if (extruding) {
    ;
    // Code for the extruding stepper
  }

  if (stepper_X.distanceToGo() == 0 && stepper_Y.distanceToGo() == 0) {
    if (x_vals.size() == 0 && y_vals.size() == 0) {
      currentState = OFF;
      extruding = false;
      ws.cleanupClients();
      return;
    }
    if (x_vals.size() > 0) {
      next_x = x_vals[0];
      x_vals.remove(0);
      stepper_X.moveTo(next_x);
    }
    if (y_vals.size() > 0) {
      next_y = y_vals[0];
      y_vals.remove(0);
      stepper_Y.moveTo(next_y * y_adjustment);
    }
    // We've moved to the first point
    if ((x_vals.size() == array_length - 1) && (y_vals.size() == array_length - 1)) {
      extruding = true;
    }
  }
}

void loop()
{
  switch (currentState)
  {
  case CALIBRATE:
    calibration();
    break;
  case ICE:
    icing();
    break;
  case OFF:
  default:
    break;
  }
  if (millis() % 5000 == 0) {
    Serial.println("We are in:");
    Serial.println(currentState);
  }
}