void setup() {
  pinMode(PIN_PA5, OUTPUT);
}

void loop() {
  digitalWrite(PIN_PA5, HIGH);
  delay(300);
  digitalWrite(PIN_PA5, LOW);
  delay(300);
}
