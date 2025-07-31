#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> 
#include <AccelStepper.h>
#include <ESPmDNS.h>

// --- Machine States ---
// Defines the high-level operational states of your machine
enum MachineState {
  STATE_IDLE,                 // Machine is doing nothing, waiting for commands
  STATE_HOMING_X,             // Starting homing for X-axis
  STATE_WAITING_X_HOME,       // Waiting for X-axis to hit limit switch
  STATE_HOMING_Y,             // Starting homing for Y-axis
  STATE_WAITING_Y_HOME,       // Waiting for Y-axis to hit limit switch
  STATE_DRAWING_INIT,         // Preparing to draw (setting first point)
  STATE_DRAWING_MOVING,       // Actively moving and drawing
  STATE_DRAWING_COMPLETE      // Drawing process finished
};
MachineState currentState = STATE_IDLE; // Initial state

// --- Pin Definitions ---
// IMPORTANT: Adjust these pins to match your actual wiring and ESP32 board!
// Avoid using pins 6-11 (ESP32 uses them for flash) or strapping pins during boot (0, 2, 5, 12, 15).
// Good choices are 13, 14, 16, 17, 21, 22, 23, 25, 26, 27, 32, 33 (check documentation for input-only pins like 34-39).
#define X_STEP_PIN 26
#define X_DIR_PIN 25
#define X_LIMIT_SWITCH_PIN 33 // Changed from 3 to a safer GPIO pin

#define Y_STEP_PIN 18
#define Y_DIR_PIN 19
#define Y_LIMIT_SWITCH_PIN 32 // Changed from 5 to a safer GPIO pin

// --- Stepper Motor Instances ---
// AccelStepper(driverType, stepPin, dirPin)
// We assume DRIVER type (separate step and dir pins)
AccelStepper stepper_X(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
AccelStepper stepper_Y(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);

// --- Stepper Configuration ---
// These are crucial for accurate movement. Adjust based on your setup:
// For a 1.8 degree motor (200 steps/revolution) with 1/16 microstepping: 200 * 16 = 3200 steps/revolution.
// If your pulley is 20 teeth and your belt pitch is 2mm (GT2), then 20 teeth * 2mm/tooth = 40mm per revolution.
// So, STEPS_PER_MM = 3200 steps / 40mm = 80 steps/mm.
const float STEPS_PER_MM_X = 80.0; // Adjust this value!
const float STEPS_PER_MM_Y = 80.0; // Adjust this value!

const int HOMING_SPEED_STEPS_PER_SEC = 400; // Slower speed for homing
const int DRAWING_MAX_SPEED_STEPS_PER_SEC = 2000; // Max speed for drawing movements
const int DRAWING_ACCELERATION_STEPS_PER_SEC_SQ = 1000; // Acceleration for drawing

// --- WiFi & Web Server ---
const char* ssid = "MAKERSPACE";     // Replace with your network credentials
const char* password = "12345678"; // Replace with your network credentials
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- JSON Buffer for WebSocket Data ---
// Allocate the JSON document. Increase size if your coordinate arrays are very large.
StaticJsonDocument<4096> doc; // 4KB should be enough for many points

// --- Drawing Data ---
JsonArray x_coords; // Stores X coordinates (in mm) received from WebSocket
JsonArray y_coords; // Stores Y coordinates (in mm) received from WebSocket
int current_point_index = 0; // Tracks which point in the drawing sequence we are currently moving to

// --- HTML Content for Web Interface ---
// Adjusted default values in HTML to be more realistic for mm coordinates.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>Stepper Plotter Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <link rel="icon" href="data:," />
    <style>
      html { font-family: Arial, Helvetica, sans-serif; text-align: center; }
      body { margin: 0; }
      .content { padding: 30px; max-width: 90vw; margin: 0 auto; }
      button {
        border: 2px solid rgb(255, 111, 0);
        background: rgba(255, 111, 0, 0.6);
        padding: 10px;
        margin: 10px;
        cursor: pointer;
        border-radius: 4px;
        display: block;
        width: 150px; /* Consistent button width */
        margin-left: auto;
        margin-right: auto;
      }
      form { margin-top: 20px; padding: 10px; border: 1px solid #ccc; border-radius: 5px; }
      label { display: inline-block; width: 80px; text-align: right; margin-right: 10px; }
      input[type="text"] { width: 80px; margin-bottom: 10px; }
      #pointsSvg { height: 300px; width: 300px; text-align: center; border: 1px solid #ddd; margin: 20px auto; }
    </style>
  </head>
  <body>
    <div class="content">
      <button onclick="calibrate()">Calibrate to (0,0)</button>
      <br />
      <form id="pointsForm" accept-charset="utf-8">
        <label for="cx">Center (x mm)</label>
        <input type="text" id="cx" name="cx" value="100.0" onchange="myDrawing.updatePoints()"/><br>
        <label for="cy">Center (y mm)</label>
        <input type="text" id="cy" name="cy" value="100.0" onchange="myDrawing.updatePoints()"/><br>
        <label for="c_rad">Radius (mm)</label>
        <input type="text" id="c_rad" name="c_rad" value="50.0" onchange="myDrawing.updatePoints()"/><br>
        <label for="n_steps">Segments</label>
        <input type="text" id="n_steps" name="n_steps" value="100" onchange="myDrawing.updatePoints()"/><br>
        <button type="button" onclick="formatSend();">Draw Circle</button>
      </form>
      <button type="button" onclick="lineX();">Draw X line (50mm)</button>
      <button type="button" onclick="lineY();">Draw Y line (50mm)</button>

      <div style="display: flex; justify-content: center; align-items: center">
        <div id="pointsSvg"></div>
      </div>
    </div>
    <script src="https://d3js.org/d3.v7.min.js"></script>
    <script src="https://d3js.org/d3-array.v2.min.js"></script>
    <script>
      var gateway = `ws://${window.location.hostname}/ws`;
      var websocket;
      window.addEventListener("load", onLoad);

      function initWebSocket() {
        console.log("Trying to open a WebSocket connection...");
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
      }
      function onOpen(event) { console.log("Connection opened"); }
      function onClose(event) { console.log("Connection closed"); setTimeout(initWebSocket, 2000); }
      function onMessage(event) { console.log("WS message:", event.data); }

      function onLoad(event) { initWebSocket(); }

      function formatSend() {
        let coords = generateCircleXY();
        console.log("Sending Circle:", JSON.stringify({mode:"draw", xy: coords})); // Use "draw" for clarity
        websocket.send(JSON.stringify({mode:"draw", xy: coords}));
      }

      function lineX() {
        // Example: Draw a 50mm line along X-axis starting from (10, 10)
        let coords = {x: [10.0, 60.0], y: [10.0, 10.0]};
        console.log("Sending X Line:", JSON.stringify({mode:"draw", xy: coords}));
        websocket.send(JSON.stringify({mode:"draw", xy: coords}));
      }

      function lineY() {
        // Example: Draw a 50mm line along Y-axis starting from (10, 10)
        let coords = {x: [10.0, 10.0], y:[10.0, 60.0]};
        console.log("Sending Y Line:", JSON.stringify({mode:"draw", xy: coords}));
        websocket.send(JSON.stringify({mode:"draw", xy: coords}));
      }

      function calibrate() {
        websocket.send(JSON.stringify({ mode: "calibrate" })); // Send a simple calibrate command
      }

      function generateCircleXY() {
        let form = document.getElementById("pointsForm");
        let formData = new FormData(form);
        let data = {};
        formData.forEach((value, key) => { data[key] = parseFloat(value); }); // Parse as float

        let theta_steps = Array.from(
          { length: data.n_steps },
          (v, i) => (2 * Math.PI / data.n_steps) * i
        );
        // Calculate X,Y coordinates in millimeters directly
        let x_vals = theta_steps.map(angle => data.c_rad * Math.cos(angle) + data.cx);
        let y_vals = theta_steps.map(angle => data.c_rad * Math.sin(angle) + data.cy);

        // Add the first point to the end to close the circle
        x_vals.push(x_vals[0]);
        y_vals.push(y_vals[0]);

        // Round to 3 decimal places for precision in transmission
        return { x: x_vals.map(val => parseFloat(val.toFixed(3))), y: y_vals.map(val => parseFloat(val.toFixed(3))) };
      }

      class drawPoints {
        constructor(parentElement, coords) {
          this.parentElement = parentElement;
          this.coords = coords;
          this.initVis();
        }

        initVis() {
          let vis = this;
          let parentWidth = 300; // Match SVG container width/height
          let parentHeight = 300;
          vis.pointSize = 2;

          vis.margin = { top: 0, right: 0, bottom: 0, left: 0 };
          vis.width = parentWidth - vis.margin.left - vis.margin.right;
          vis.height = parentHeight - vis.margin.top - vis.margin.bottom;

          vis.svg = d3
            .select(`#${vis.parentElement}`)
            .append("svg")
            .attr("viewBox", `0 0 ${vis.width + vis.margin.left + vis.margin.right} ${vis.height + vis.margin.top + vis.margin.bottom}`)
            .attr("preserveAspectRatio", "xMidYMid meet")
            .append("g")
            .attr("transform", `translate(${vis.margin.left},${vis.margin.top})`);
          
          // Draw a boundary box (e.g., representing your plotter's work area)
          vis.svg.append("rect")
            .attr("x", 0)
            .attr("y", 0)
            .attr("width", vis.width)
            .attr("height", vis.height)
            .attr("fill", "none")
            .attr("stroke", "lightgray")
            .attr("stroke-width", 1);

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

      let init_drawing_coords = generateCircleXY();
      let myDrawing = new drawPoints("pointsSvg", init_drawing_coords.x.map((x, i) => ({ x: x, y: init_drawing_coords.y[i] })));
    </script>
  </body>
</html>
)rawliteral";

// Initialize WiFi
void initWiFi() {
  Serial.println("initWiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi ..");
  unsigned long wifiConnectStart = millis();
  // Use millis() for non-blocking WiFi connection with a timeout
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiConnectStart < 30000)) { // 30-second timeout
    Serial.print('.');
    delay(500); // Small delay to prevent tight loop, but still non-blocking for long operations
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi after 30 seconds. Please check credentials or reset.");
    // In a real application, you might want to restart ESP32 here or go into a recovery mode.
  }

  // Start mDNS for easy access (e.g., http://mrfrosty.local)
  if (!MDNS.begin("mrfrosty")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started: mrfrosty.local");
  }
}

// WebSocket message handler
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    // Null-terminate the received data for string operations
    // Note: This modifies the incoming data buffer. If data is const, copy it first.
    // For ArduinoJson, it's often better to parse directly from the buffer.
    // data[len] = 0; // Don't null-terminate if parsing directly as byte array

    // Deserialize the JSON document
    // Using the byte array directly might be more robust for larger messages
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      // Optionally send an error back to the client
      ws.textAll("Error parsing JSON command.");
      return;
    }

    String mode_string = doc["mode"].as<String>();
    Serial.print("Received mode: ");
    Serial.println(mode_string);

    if (mode_string == "draw") { // Changed "ice" to "draw" for clarity
      x_coords = doc["xy"]["x"];
      y_coords = doc["xy"]["y"];
      current_point_index = 0; // Reset index for new drawing
      currentState = STATE_DRAWING_INIT; // Start drawing sequence
      Serial.println("Drawing command received.");
      Serial.print("X points to draw: "); Serial.println(x_coords.size());
      Serial.print("Y points to draw: "); Serial.println(y_coords.size());
      if (x_coords.size() != y_coords.size()) {
        Serial.println("WARNING: X and Y coordinate arrays have different sizes!");
        // You might want to handle this error more gracefully
      }

    } else if (mode_string == "calibrate") {
      Serial.println("Calibration command received. Starting homing sequence...");
      currentState = STATE_HOMING_X; // Initiate X-axis homing
    } else {
      currentState = STATE_IDLE; // Default to idle if unknown command
      Serial.println("Unknown command mode received. Setting state to IDLE.");
    }
  }
}

// WebSocket event handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      // Optionally send current machine state to the new client
      // ws.text(client->id(), "Machine is " + String(currentState));
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      Serial.printf("WS Error or Pong: type=%d\n", type);
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// Processor for HTML template variables (not strictly needed for this HTML, but kept for structure)
String processor(const String& var) {
  // If you had placeholders like %STATE% in your HTML, this would replace them.
  // Example: if (var == "STATE") { return String(currentState); }
  return String();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting up Stepper Plotter...");

  // Initialize WiFi and mDNS
  initWiFi();
  // Initialize WebSockets
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.begin(); // Start the web server

  // Configure limit switch pins
  pinMode(X_LIMIT_SWITCH_PIN, INPUT_PULLUP); // Use INPUT_PULLUP if switches connect to GND when pressed
  pinMode(Y_LIMIT_SWITCH_PIN, INPUT_PULLUP);

  // Set initial stepper speeds and accelerations for drawing
  stepper_X.setMaxSpeed(DRAWING_MAX_SPEED_STEPS_PER_SEC);
  stepper_X.setAcceleration(DRAWING_ACCELERATION_STEPS_PER_SEC_SQ);
  stepper_Y.setMaxSpeed(DRAWING_MAX_SPEED_STEPS_PER_SEC);
  stepper_Y.setAcceleration(DRAWING_ACCELERATION_STEPS_PER_SEC_SQ);

  Serial.println("Setup complete. Waiting for commands.");
}


void loop() {
  // Always call ws.cleanupClients() to manage WebSocket connections (non-blocking)
  ws.cleanupClients();

  // Always run steppers to allow movement commands to execute (non-blocking)
  stepper_X.run();
  stepper_Y.run();

  // Main State Machine for Machine Operation
  switch (currentState) {
    case STATE_IDLE:
      // Machine is idle, waiting for commands via WebSocket.
      // Could send status updates periodically, or just wait.
      break;

    case STATE_HOMING_X:
      Serial.println("Homing X-axis: Moving towards limit switch...");
      // Set X stepper to run at homing speed in the negative direction
      stepper_X.setSpeed(-HOMING_SPEED_STEPS_PER_SEC);
      currentState = STATE_WAITING_X_HOME; // Transition to waiting state
      break;

    case STATE_WAITING_X_HOME:
      // Wait for X limit switch to be pressed (LOW)
      if (digitalRead(X_LIMIT_SWITCH_PIN) == LOW) {
        stepper_X.stop(); // Stop the motor
        stepper_X.setCurrentPosition(0); // Set current position to 0 (origin)
        Serial.println("X-axis homed to 0.");
        currentState = STATE_HOMING_Y; // Move to homing Y-axis
      }
      // If the switch isn't pressed, stepper_X.run() keeps it moving
      break;

    case STATE_HOMING_Y:
      Serial.println("Homing Y-axis: Moving towards limit switch...");
      // Set Y stepper to run at homing speed in the negative direction
      stepper_Y.setSpeed(-HOMING_SPEED_STEPS_PER_SEC);
      currentState = STATE_WAITING_Y_HOME; // Transition to waiting state
      break;

    case STATE_WAITING_Y_HOME:
      // Wait for Y limit switch to be pressed (LOW)
      if (digitalRead(Y_LIMIT_SWITCH_PIN) == LOW) {
        stepper_Y.stop(); // Stop the motor
        stepper_Y.setCurrentPosition(0); // Set current position to 0 (origin)
        Serial.println("Y-axis homed to 0.");
        
        // Restore drawing speeds after homing
        stepper_X.setMaxSpeed(DRAWING_MAX_SPEED_STEPS_PER_SEC);
        stepper_X.setAcceleration(DRAWING_ACCELERATION_STEPS_PER_SEC_SQ);
        stepper_Y.setMaxSpeed(DRAWING_MAX_SPEED_STEPS_PER_SEC);
        stepper_Y.setAcceleration(DRAWING_ACCELERATION_STEPS_PER_SEC_SQ);

        Serial.println("All axes homed. Ready to draw.");
        currentState = STATE_IDLE; // Return to idle state after homing
        ws.textAll("Homing Complete! Machine is ready."); // Notify connected clients
      }
      // If the switch isn't pressed, stepper_Y.run() keeps it moving
      break;

    case STATE_DRAWING_INIT:
      // This state is just to set the first target and transition to active drawing
      if (x_coords.size() > 0 && y_coords.size() > 0) {
        // Convert first coordinate from mm to steps
        long target_x_steps = round(x_coords[0].as<float>() * STEPS_PER_MM_X);
        long target_y_steps = round(y_coords[0].as<float>() * STEPS_PER_MM_Y);
        stepper_X.moveTo(target_x_steps);
        stepper_Y.moveTo(target_y_steps);
        Serial.printf("Drawing: Moving to first point (%.2fmm, %.2fmm) -> (%ld steps, %ld steps)\n",
                      x_coords[0].as<float>(), y_coords[0].as<float>(), target_x_steps, target_y_steps);
        current_point_index = 0; // Ensure index starts at 0 for the first point
        currentState = STATE_DRAWING_MOVING; // Start actively moving
      } else {
        Serial.println("Error: No drawing points received or arrays are empty.");
        currentState = STATE_IDLE; // Go back to idle if no points
        ws.textAll("Error: No drawing data.");
      }
      break;

    case STATE_DRAWING_MOVING:
      // Check if both X and Y steppers have reached their current target
      if (stepper_X.distanceToGo() == 0 && stepper_Y.distanceToGo() == 0) {
        current_point_index++; // Move to the next point in the array

        if (current_point_index < x_coords.size() && current_point_index < y_coords.size()) {
          // If there are more points, set the next target
          float next_x_mm = x_coords[current_point_index].as<float>();
          float next_y_mm = y_coords[current_point_index].as<float>();
          long target_x_steps = round(next_x_mm * STEPS_PER_MM_X);
          long target_y_steps = round(next_y_mm * STEPS_PER_MM_Y);
          stepper_X.moveTo(target_x_steps);
          stepper_Y.moveTo(target_y_steps);
          Serial.printf("Drawing: Moving to point %d (%.2fmm, %.2fmm) -> (%ld steps, %ld steps)\n",
                        current_point_index, next_x_mm, next_y_mm, target_x_steps, target_y_steps);
        } else {
          // All points drawn, complete the drawing
          currentState = STATE_DRAWING_COMPLETE;
        }
      }
      break;

    case STATE_DRAWING_COMPLETE:
      Serial.println("Drawing sequence complete!");
      currentState = STATE_IDLE; // Return to idle
      ws.textAll("Drawing Complete!"); // Notify clients
      break;
  }
}
