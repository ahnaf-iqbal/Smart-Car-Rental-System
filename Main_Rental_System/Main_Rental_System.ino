/*
 ===============================================================================
  Project: Smart Car Rental System (BRAC University - CSE461 Robotics Lab)
  Group:   Group 05 | Section 08
  Board:   Arduino Uno R3 (Main Controller)

  Features:
   - RFID RC522 Authentication (Authorized Card validation)
   - Automatic Barrier Gate control using SG90 Servo (0° Closed, 90° Open)
   - Dual IR Sensors for Entry (IR1) and Exit (IR2) detection
   - 16x2 I2C LCD Display for real-time status, rental duration & fare calculation
   - Audio-visual feedback: Green LED (Granted), Red LED & Buzzer (Denied alert)
   - Rental Fare Calculation: Duration (seconds) * 10 BDT (Customizable rate)
 ===============================================================================

  Wiring / Pin Mapping:
  ------------------------------------------------------------
  Component         Arduino Uno Pin    Notes
  ------------------------------------------------------------
  RFID RC522 SDA(SS)   Digital 10         SPI Slave Select
  RFID RC522 SCK       Digital 13         SPI Clock
  RFID RC522 MOSI      Digital 11         SPI Master Out
  RFID RC522 MISO      Digital 12         SPI Master In
  RFID RC522 RST       Digital 7          Reset pin
  RFID RC522 3.3V      3.3V               *DO NOT connect to 5V!*
  RFID RC522 GND       GND

  I2C LCD 16x2 SDA     Analog A4          I2C Data
  I2C LCD 16x2 SCL     Analog A5          I2C Clock
  I2C LCD VCC          5V
  I2C LCD GND          GND

  Servo Signal (Gate)  Digital 9 (PWM)    0° = Closed, 90° = Open
  Servo VCC            5V
  Servo GND            GND

  IR Sensor 1 (Entry)  Digital 2          Active LOW when car detected
  IR Sensor 2 (Exit)   Digital 3          Active LOW when car detected

  Green LED (Access)   Digital 4          Series 220Ω resistor to GND
  Red LED (Alert)      Digital 5          Series 220Ω resistor to GND
  Buzzer (Alarm)       Digital 6          Positive pin; GND to Arduino GND
 ===============================================================================
*/

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// --- PIN DEFINITIONS ---
#define SS_PIN          10
#define RST_PIN         7
#define SERVO_PIN       9
#define IR_ENTRY_PIN    2
#define IR_EXIT_PIN     3
#define LED_GREEN       4
#define LED_RED         5
#define BUZZER_PIN      6

// --- SERVO GATE ANGLES ---
#define GATE_LOCKED_POS   0   // Barrier closed
#define GATE_UNLOCKED_POS 90  // Barrier open

// --- FARE CONFIGURATION ---
// As documented in report: Fare = time * 10 (BDT/TK)
const unsigned long FARE_RATE_PER_SECOND = 10; 

// --- HARDWARE OBJECT INSTANCES ---
MFRC522 mfrc522(SS_PIN, RST_PIN);
// Most standard 1602 I2C modules use address 0x27 (or 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Servo gateServo;

// --- AUTHORIZED RFID CARDS LIST ---
// Format: byte arrays of 4-byte or 7-byte UIDs.
// Update these with your physical card / keyfob UIDs!
const byte AUTHORIZED_CARDS[][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF}, // Example Card 1
  {0x33, 0xA1, 0x5C, 0x29}, // Example Card 2
  {0x79, 0x62, 0x48, 0x02}  // Example Card 3
};
const int TOTAL_AUTHORIZED_CARDS = sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

// --- SYSTEM STATES ---
enum SystemState {
  STATE_STANDBY,     // Waiting for customer / car
  STATE_CAR_RENTED,   // Car is currently rented out
  STATE_CAR_RETURNED  // Car returned, showing billing summary
};

SystemState currentState = STATE_STANDBY;

// Active rental variables
unsigned long rentalStartTime = 0;
unsigned long rentalEndTime = 0;
byte activeUserUID[4];
bool hasActiveRental = false;

// Function prototypes
bool isCardAuthorized(byte *uid, byte uidSize);
bool isSameCard(byte *uid1, byte *uid2, byte size);
void openGate();
void closeGate();
void triggerAccessGrantedAlert();
void triggerAccessDeniedAlert();
void displayStandbyScreen();
void displayRentedScreen();
void displayReturnedScreen(unsigned long durationSec, unsigned long fare);

void setup() {
  Serial.begin(9600);
  while (!Serial) { ; } // Wait for serial port to connect

  Serial.println(F("========================================"));
  Serial.println(F("  CSE461 - SMART CAR RENTAL SYSTEM      "));
  Serial.println(F("========================================"));

  // Initialize Pin Modes
  pinMode(IR_ENTRY_PIN, INPUT);
  pinMode(IR_EXIT_PIN, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize Servo
  gateServo.attach(SERVO_PIN);
  closeGate(); // Ensure gate starts locked

  // Initialize SPI & MFRC522 RFID
  SPI.begin();
  mfrc522.PCD_Init();
  delay(100);
  mfrc522.PCD_DumpVersionToSerial();

  // Initialize I2C LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Car Rental");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1500);

  displayStandbyScreen();
  Serial.println(F("[SYSTEM READY] Waiting for RFID or Car..."));
}

void loop() {
  // Check if an RFID card is swiped
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    handleRFIDScan();
    // Halt PICC to prepare for next read
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  // Optional: Monitor IR entry/exit presence for gate auto-close
  delay(50);
}

// -------------------------------------------------------------
// RFID Scan Handler
// -------------------------------------------------------------
void handleRFIDScan() {
  Serial.print(F("[RFID] Detected UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) Serial.print(F("0"));
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(F(" "));
  }
  Serial.println();

  // Check authorization
  if (!isCardAuthorized(mfrc522.uid.uidByte, mfrc522.uid.size)) {
    Serial.println(F("[ALERT] Unauthorized Card! Access Denied."));
    triggerAccessDeniedAlert();
    displayStandbyScreen();
    return;
  }

  // Card is AUTHORIZED
  if (!hasActiveRental) {
    // ---------------------------------------------------------
    // ACTION 1: START RENTAL (CAR CHECKOUT)
    // ---------------------------------------------------------
    Serial.println(F("[ACCESS GRANTED] Starting new car rental..."));
    hasActiveRental = true;
    rentalStartTime = millis();

    for (byte i = 0; i < 4; i++) {
      activeUserUID[i] = mfrc522.uid.uidByte[i];
    }

    triggerAccessGrantedAlert();
    displayRentedScreen();
    openGate();

    // Wait for vehicle to pass the entry IR sensor
    Serial.println(F("[ENTRY] Waiting for vehicle to pass Entry IR..."));
    unsigned long waitStart = millis();
    bool passed = false;

    // Timeout after 8 seconds if no physical car crosses
    while (millis() - waitStart < 8000) {
      if (digitalRead(IR_ENTRY_PIN) == LOW) { // IR sensor active LOW
        Serial.println(F("[ENTRY] Car passed Entry IR Sensor."));
        passed = true;
        delay(1200); // Give time for rear wheels to pass
        break;
      }
      delay(50);
    }

    if (!passed) {
      Serial.println(F("[TIMEOUT] Auto-closing gate after delay."));
    }

    closeGate();
    digitalWrite(LED_GREEN, LOW);

    // Update LCD for active trip
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Car On Trip...");
    lcd.setCursor(0, 1);
    lcd.print("Tracking Active");

  } else {
    // ---------------------------------------------------------
    // ACTION 2: END RENTAL (CAR RETURN & BILLING)
    // ---------------------------------------------------------
    // Check if the same card is being used to return
    if (!isSameCard(mfrc522.uid.uidByte, activeUserUID, 4)) {
      Serial.println(F("[WARNING] Different card swiped while car is rented!"));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Card Mismatch!");
      lcd.setCursor(0, 1);
      lcd.print("Use Rented Card");
      triggerAccessDeniedAlert();
      delay(2000);
      return;
    }

    Serial.println(F("[RETURN] Processing vehicle return..."));
    rentalEndTime = millis();

    // Duration calculation in seconds
    unsigned long durationSeconds = (rentalEndTime - rentalStartTime) / 1000;
    if (durationSeconds == 0) durationSeconds = 1; // Minimum charge 1s
    unsigned long totalFare = durationSeconds * FARE_RATE_PER_SECOND;

    Serial.print(F("Rental Duration: "));
    Serial.print(durationSeconds);
    Serial.println(F(" seconds"));

    Serial.print(F("Total Fare: "));
    Serial.print(totalFare);
    Serial.println(F(" TK"));

    // Display billing summary
    triggerAccessGrantedAlert();
    displayReturnedScreen(durationSeconds, totalFare);
    openGate();

    // Wait for vehicle to clear exit IR sensor
    Serial.println(F("[EXIT] Waiting for car to clear Exit IR..."));
    unsigned long waitStart = millis();
    bool passed = false;

    while (millis() - waitStart < 8000) {
      if (digitalRead(IR_EXIT_PIN) == LOW) { // IR sensor active LOW
        Serial.println(F("[EXIT] Car passed Exit IR Sensor."));
        passed = true;
        delay(1200); // Time to clear gate
        break;
      }
      delay(50);
    }

    if (!passed) {
      Serial.println(F("[TIMEOUT] Auto-closing gate after return delay."));
    }

    closeGate();
    digitalWrite(LED_GREEN, LOW);

    // Keep fare displayed for 4 seconds so user can read it clearly
    delay(4000);

    // Reset state for next customer
    hasActiveRental = false;
    rentalStartTime = 0;
    rentalEndTime = 0;
    displayStandbyScreen();
  }
}

// -------------------------------------------------------------
// Authorization & Verification Helpers
// -------------------------------------------------------------
bool isCardAuthorized(byte *uid, byte uidSize) {
  for (int i = 0; i < TOTAL_AUTHORIZED_CARDS; i++) {
    bool match = true;
    for (byte j = 0; j < 4 && j < uidSize; j++) {
      if (uid[j] != AUTHORIZED_CARDS[i][j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool isSameCard(byte *uid1, byte *uid2, byte size) {
  for (byte i = 0; i < size; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

// -------------------------------------------------------------
// Gate Actuation
// -------------------------------------------------------------
void openGate() {
  Serial.println(F("[SERVO] Gate Opening (90 deg)"));
  gateServo.write(GATE_UNLOCKED_POS);
  delay(500);
}

void closeGate() {
  Serial.println(F("[SERVO] Gate Closing (0 deg)"));
  gateServo.write(GATE_LOCKED_POS);
  delay(500);
}

// -------------------------------------------------------------
// Audio-Visual Feedback
// -------------------------------------------------------------
void triggerAccessGrantedAlert() {
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, LOW);
  // Short pleasant double beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  delay(80);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(120);
  digitalWrite(BUZZER_PIN, LOW);
}

void triggerAccessDeniedAlert() {
  digitalWrite(LED_GREEN, LOW);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Denied!");
  lcd.setCursor(0, 1);
  lcd.print("Invalid Card");

  // Triple alarm beep with red LED flash
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
  delay(1000);
}

// -------------------------------------------------------------
// LCD Display Helpers
// -------------------------------------------------------------
void displayStandbyScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CAR RENTAL SYSTEM");
  lcd.setCursor(0, 1);
  lcd.print("Scan RFID Card..");
}

void displayRentedScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Car Rented!");
  lcd.setCursor(0, 1);
  lcd.print("Drive Safe!");
}

void displayReturnedScreen(unsigned long durationSec, unsigned long fare) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  lcd.print(durationSec);
  lcd.print(" sec");

  lcd.setCursor(0, 1);
  lcd.print("Fare: ");
  lcd.print(fare);
  lcd.print(" TK");
}
