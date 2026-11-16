/*
 ===============================================================================
  Project: Smart Car Rental System (BRAC University - CSE461 Robotics Lab)
  Group:   Group 05 | Section 08
  Board:   Arduino Uno R3 (Camera Stepper Controller)

  Features:
   - 28BYJ-48 5V Stepper Motor + ULN2003 Driver Board
   - 10kΩ Potentiometer on Analog Pin A0 for real-time camera pan / tilt angle
   - Solves power consumption & SPI MISO/MOSI pin conflicts by running on
     a dedicated secondary Arduino Uno board as described in the report.
 ===============================================================================

  Wiring / Pin Mapping:
  ------------------------------------------------------------
  Component             Arduino Uno Pin    Notes
  ------------------------------------------------------------
  ULN2003 IN1           Digital 8          Stepper Phase 1
  ULN2003 IN2           Digital 9          Stepper Phase 2
  ULN2003 IN3           Digital 10         Stepper Phase 3
  ULN2003 IN4           Digital 11         Stepper Phase 4
  ULN2003 5V (VCC)      5V (External/Uno)  External 5V supply recommended
  ULN2003 GND           GND                Common Ground with Arduino

  Potentiometer Pin 1   5V
  Potentiometer Wiper   Analog A0          Analog Voltage reading (0 - 1023)
  Potentiometer Pin 3   GND
 ===============================================================================
*/

#include <Stepper.h>

// 28BYJ-48 stepper motor has 2048 steps per full 360 degree revolution (in 4-step mode)
const int STEPS_PER_REV = 2048;

// Desired maximum sweep angle for surveillance camera (e.g. 180 degrees = 1024 steps)
const int MAX_CAMERA_STEPS = 1024; 

// Stepper sequence for ULN2003: IN1, IN3, IN2, IN4 (standard Arduino Stepper library order)
Stepper cameraStepper(STEPS_PER_REV, 8, 10, 9, 11);

const int POT_PIN = A0;
int previousPotVal = -1;
int currentStepPosition = 0;

void setup() {
  Serial.begin(9600);
  pinMode(POT_PIN, INPUT);

  // Set stepper speed (RPM: 5 to 15 is standard for 28BYJ-48)
  cameraStepper.setSpeed(12);

  Serial.println(F("============================================="));
  Serial.println(F("  CSE461 - STEPPER CAMERA PAN CONTROLLER     "));
  Serial.println(F("============================================="));
  Serial.println(F("[READY] Rotate 10k potentiometer to adjust camera angle..."));
}

void loop() {
  // Read analog value from 10k potentiometer (0 - 1023)
  int potValue = analogRead(POT_PIN);

  // Filter out minor analog noise/jitter
  if (abs(potValue - previousPotVal) > 4) {
    // Map potentiometer reading to target stepper position
    int targetStepPosition = map(potValue, 0, 1023, 0, MAX_CAMERA_STEPS);
    int stepsToMove = targetStepPosition - currentStepPosition;

    if (stepsToMove != 0) {
      Serial.print(F("[CAMERA PAN] Pot: "));
      Serial.print(potValue);
      Serial.print(F(" | Moving steps: "));
      Serial.print(stepsToMove);
      Serial.print(F(" -> Pos: "));
      Serial.println(targetStepPosition);

      cameraStepper.step(stepsToMove);
      currentStepPosition = targetStepPosition;
    }

    previousPotVal = potValue;
  }

  delay(20);
}
