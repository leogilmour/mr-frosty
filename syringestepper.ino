#define DIR_PIN 2
#define STEP_PIN 3
#define EN_PIN 6

void setup() {
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  digitalWrite(EN_PIN, LOW);  // Enable the stepper driver

  Serial.begin(115200);
  Serial.println("Ready. Send 'f' or 'b'");
}

void loop() {
  if (Serial.available()) {
    char command = Serial.read();

    if (command == 'f') {
      Serial.println("Forward");
      digitalWrite(DIR_PIN, HIGH);
      moveSteps(200);  // move forward 200 steps
    } 
    else if (command == 'b') {
      Serial.println("Backward");
      digitalWrite(DIR_PIN, LOW);
      moveSteps(200);  // move backward 200 steps
    }
  }
}

void moveSteps(int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(800);  // adjust for speed
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(800);
  }
}
