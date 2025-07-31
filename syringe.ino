const int stepPin= 3;
const int dirPin = 2;

void setup() {
  // put your setup code here, to run once:
  pinMode(dirPin, OUTPUT);
  pinMode(stepPin, OUTPUT);
  digitalWrite(dirPin, HIGH);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(stepPin, HIGH);
  delay(1);
  digitalWrite(stepPin, LOW);
  delay(1);
}
