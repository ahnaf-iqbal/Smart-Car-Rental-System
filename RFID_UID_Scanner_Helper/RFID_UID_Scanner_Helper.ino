/*
 ===============================================================================
  Project: Smart Car Rental System (BRAC University - CSE461 Robotics Lab)
  Tool:    RFID Card UID Scanner & Formatter
  Board:   Arduino Uno R3

  Purpose:
   Use this utility sketch to read the unique UID of your RFID cards, keyfobs,
   or tokens. It formats the output into C++ byte array syntax so you can
   copy and paste directly into `Main_Rental_System_Arduino.ino`.
 ===============================================================================

  Wiring:
  ------------------------------------------------------------
  RC522 Pin        Arduino Uno Pin
  ------------------------------------------------------------
  SDA (SS)         Digital 10
  SCK              Digital 13
  MOSI             Digital 11
  MISO             Digital 12
  RST              Digital 7 (or 9)
  3.3V             3.3V (*Never 5V!*)
  GND              GND
 ===============================================================================
*/

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 10
#define RST_PIN 7

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(9600);
  while (!Serial);

  SPI.begin();
  rfid.PCD_Init();
  delay(100);

  Serial.println(F("=================================================="));
  Serial.println(F("       RFID RC522 UID SCANNER & FORMATTER         "));
  Serial.println(F("=================================================="));
  Serial.println(F("Swipe any RFID card or keyfob to read its UID...\n"));
}

void loop() {
  // Look for new cards
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  Serial.println(F("---------------------------------------------"));
  Serial.print(F("Card Detected! Size: "));
  Serial.print(rfid.uid.size);
  Serial.println(F(" bytes"));

  // Print raw hex
  Serial.print(F("UID (Hex): "));
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print(F("0"));
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(F(" "));
  }
  Serial.println();

  // Print C++ ready array format
  Serial.print(F("Code snippet for AUTHORIZED_CARDS: {"));
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(F("0x"));
    if (rfid.uid.uidByte[i] < 0x10) Serial.print(F("0"));
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(F(", "));
  }
  Serial.println(F("}"));
  Serial.println(F("---------------------------------------------\n"));

  // Halt PICC & Stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(1000);
}
