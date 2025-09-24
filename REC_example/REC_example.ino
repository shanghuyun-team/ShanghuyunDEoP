/*
   ESP32 + ISD1820-S16 Voice Module
   Pins:
     ISD1820 REC   -> GPIO 18
     ISD1820 PLAYE -> GPIO 19
     ISD1820 PLAYL -> GPIO 21
     ISD1820 VCC   -> 3.3V (or 5V if your module requires)
     ISD1820 GND   -> GND
*/

#define REC_PIN   3
#define PLAYL_PIN 7

void setup() {
  Serial.begin(115200);
  pinMode(REC_PIN, OUTPUT);
  pinMode(PLAYL_PIN, OUTPUT);

  // default idle LOW
  digitalWrite(REC_PIN, LOW);
  digitalWrite(PLAYL_PIN, LOW);

  Serial.println("ESP32 ISD1820 test ready!");
}

void loop() {
  // Record for 5 seconds
  Serial.println("Recording...");
  digitalWrite(REC_PIN, HIGH);
  delay(5000);
  digitalWrite(REC_PIN, LOW);

  delay(6000); // wait until playback done

  // Play in loop using PLAYL (level triggered)
  Serial.println("Play loop...");
  digitalWrite(PLAYL_PIN, HIGH);
  delay(8000); // loop for 8 sec
  digitalWrite(PLAYL_PIN, LOW);

  delay(5000);
}
