// Teensy 3.2 DAC Pin
const int dacPin = A14;

void setup() {
  // 12-Bit Auflösung (0 bis 4095)
  analogWriteResolution(12);
  
  // Serial starten (Geschwindigkeit ist beim Teensy USB egal, er nimmt immer max speed)
  Serial.begin(9600);
}

void loop() {
  // --- Zeitbasis ---
  float t = millis() / 1000.0;

  // --- Einstellungen ---
  float frequenz = 1.0; // 1 Hz

  // --- Berechnung für 2V Amplitude ---
  // 3.3V = 4095 Einheiten
  // 2.0V = (2.0 / 3.3) * 4095 = ca. 2482 Einheiten (Gesamthub)
  // Amplitude = Gesamthub / 2 = 1241 Einheiten
  
  float amplitude = 25.0; 
  float offset = 204.0;   

  // --- Signal generieren ---
  float sineValue = offset + amplitude * sin(2 * PI * frequenz * t);

  // --- 1. Physische Ausgabe am DAC Pin ---
  analogWrite(dacPin, (int)sineValue);

  // --- 2. Ausgabe für den Serial Plotter ---
  // Format: "Wert1 Wert2 Wert3" (getrennt durch Leerzeichen oder Komma)
  
  Serial.print(4095);      // Referenzlinie Oben (3.3V)
  Serial.print(" ");       // Trennzeichen
  Serial.print(0);         // Referenzlinie Unten (0V)
  Serial.print(" ");       // Trennzeichen
  Serial.println((int)sineValue); // Das eigentliche Signal

  // --- WICHTIG für den Plotter ---
  // Der Plotter braucht Zeit zum Zeichnen. Wenn wir zu schnell senden,
  // verschluckt er sich. 20ms Delay = 50 Punkte pro Sekunde (reicht für 1 Hz locker).
  delay(20); 
}