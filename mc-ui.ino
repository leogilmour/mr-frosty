#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>

AccelStepper stepper_X(1, 2, 4); // initialise accelstepper for a two wire board
AccelStepper stepper_Y(1, 19, 21); // initialise accelstepper for a two wire board

// Replace with your network credentials
const char* ssid = "MAKERSPACE";
const char* password = "12345678";

String message = "";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

//Variables to save values from HTML form
String steps_X;
String steps_Y;
int penDown = 0; 
bool newRequest = false;


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Sidewalk Plotter Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <div class="topnav">
    <h3>Sidewalk Plotter Control</h3>
  </div>
  <div class="content">
      <canvas id="canvas" width="600" height="100" style="border: 1px solid black;">
          Your browser does not support the HTML 5 Canvas. 
      </canvas>
      <label class="switch">
        <input type="checkbox" onclick="togglePen()">
        <span class="slider round">Pen Down</span>
      </label>
  </div>
</body>
<script>
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onload);
var dir;
var pointsX = [];
var pointsY = [];
var penDown = 0; 

var canvas = document.querySelector('canvas'),
    ctx = canvas.getContext('2d');

ctx.lineWidth = 2;
ctx.strokeStyle = 'red';

//report the mouse position on click
canvas.addEventListener("click", function (evt) {
  var mousePos = getMousePos(canvas, evt);
  if (penDown == 0){
    pointsX.pop();
    pointsY.pop();
  } 
  
  pointsX.push(mousePos.x);
  pointsY.push(mousePos.y);

  websocket.send(mousePos.x+"&"+mousePos.y+"-"+penDown);
  ctx.beginPath();
  for (var i = 0; i < pointsX.length; i++){
    ctx.lineTo(pointsX[i], pointsY[i]);
  }
  ctx.stroke();    
}, false);

//Get Mouse Position
function getMousePos(canvas, evt) {
    var rect = canvas.getBoundingClientRect();
    return {
        x: evt.clientX - rect.left,
        y: evt.clientY - rect.top
    };
}

function togglePen(){
  if (penDown == 0){
    penDown = 1;
  } else {
    penDown = 0;
  }
  console.log(penDown);
}

function onload(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
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
</script>
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

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    message = (char*)data;
    steps_X = message.substring(0, message.indexOf("&"));
    steps_Y = message.substring(message.indexOf("&") + 1, message.indexOf("-"));
    penDown = message.substring(message.indexOf("-") + 1, message.length()).toInt();
    Serial.println("steps_X: ");
    Serial.println(steps_X);
    Serial.println("steps_Y: ");
    Serial.println(steps_Y);
    Serial.println("pen down: ");
    Serial.println(penDown);
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

void loop() {
  if (newRequest) {
    stepper_X.moveTo(steps_X.toInt());
    stepper_Y.moveTo(steps_Y.toInt());
    Serial.println("new");
    Serial.println(steps_X.toInt());
    newRequest = false;
    ws.cleanupClients();
  }

  if (stepper_X.distanceToGo() == 0 && stepper_Y.distanceToGo() == 0) {
    //Serial.println("done");
  }

  stepper_X.run();
  stepper_Y.run();
  
}
