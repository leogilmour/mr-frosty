#include <AccelStepper.h>

// Define pins for ESP32 (adjust based on your wiring)
#define DIR_PIN  25 // GPIO pin for Direction
#define STEP_PIN 26 // GPIO pin for Step

// AccelStepper::DRIVER indicates we are using a driver like A4988/DRV8825
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

const int stepsPerRevolution = 200; // For a 1.8 degree motor (full steps)
// Adjust for using microstepping, e.g., 200 * 8 for 1/8 microstepping

// --- Timing Variables for delays between moves ---
unsigned long lastMoveFinishedMillis = 0;
const long delayBetweenMoves = 3000; // Time in ms to wait after one move finishes before starting the next

// --- Sequence Management Variables ---
int currentMoveStep = 0; // Tracks which movement in the sequence we are performing

void setup() {
  Serial.begin(115200);
  Serial.println("AccelStepper Sequence Test");

  // Configure stepper motor properties
  stepper.setMaxSpeed(1000);      // Max speed in steps/second
  stepper.setAcceleration(500);   // Acceleration in steps/second^2 (smoother start/stop)
  stepper.setCurrentPosition(0);  // Set current position to 0

  // Start the very first movement immediately
  Serial.println("Starting 2 revolutions clockwise (initial move)...");
  stepper.moveTo(stepsPerRevolution * 2);
  lastMoveFinishedMillis = millis(); // Initialize to ensure the first wait period is correct
  currentMoveStep = 0; // Set to indicate the first move is in progress
}

void loop() {
  stepper.run();

  // Check if the stepper has reached its target AND if enough time has passed since the last move completed
  if (stepper.distanceToGo() == 0 && (millis() - lastMoveFinishedMillis >= delayBetweenMoves)) {

    // Advance to the next step in the sequence
    currentMoveStep++;

    switch (currentMoveStep) {
      case 1: // First move (2 revolutions clockwise) just completed
        Serial.println("\nFinished clockwise move. Starting 2 revolutions counter-clockwise...");
        stepper.moveTo(0); // Move back to initial position (0)
        break;

      case 2: // Second move (2 revolutions counter-clockwise) just completed
        Serial.println("Finished counter-clockwise move. Starting 400 steps relative...");
        stepper.move(400); // Move 400 steps relative to current position
        break;

      case 3: // Third move (400 steps relative) just completed
        Serial.println("Finished relative move. Resetting sequence and returning to 0...");
        stepper.moveTo(0); // Return to absolute 0 to restart the cycle
        currentMoveStep = 0; // Reset counter to loop the sequence
        break;
    }
    lastMoveFinishedMillis = millis(); // Reset timer for the delay before the *next* move
  }

  // Print stepper position every 500 milliseconds, regardless of stepper movement
  static unsigned long lastPrintMillis = 0;
  if (millis() - lastPrintMillis >= 500) {
    Serial.print("Current Stepper Pos: ");
    Serial.println(stepper.currentPosition());
    lastPrintMillis = millis();
  }
}