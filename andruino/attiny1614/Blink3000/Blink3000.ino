void setup() {
  pinMode(PIN_PA5, OUTPUT);
}

void loop() {
  digitalWrite(PIN_PA5, HIGH);
  delay(3000);
  digitalWrite(PIN_PA5, LOW);
  delay(3000);
}
