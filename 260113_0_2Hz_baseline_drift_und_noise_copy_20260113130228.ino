// Teensy 3.2 DAC Pin
const int dacPin = A14;

void setup() {
  // 12-Bit Auflösung (0-4095) für weiche Kurven
  analogWriteResolution(12);
  
  // Serial starten
  Serial.begin(115200);

  // Zufallsgenerator initialisieren (Rauschen soll zufällig sein)
  randomSeed(analogRead(A0));
}

void loop() {
  // --- Zeitbasis ---
  float t = micros() / 1000000.0; 

  // --- Parameter für Baseline Drift (Langsam) ---
  float frequenz = 0.2;       // 0.2 Hz (sehr langsam)
  float amplitude = 250.0;    // Hub der Sinuswelle
  float offset = 250.0;      // Die Mitte etwas höher setzen, Platz für Rauschen lassen

  // --- Parameter für Rauschen (Hochfrequent) ---
  // Ein Wert von 150 sorgt für deutliches "Zittern" auf der Leitung.
  int noiseLevel = 150; 

  // --- 1. Die saubere Baseline berechnen (Drift) ---
  float cleanSine = offset + amplitude * sin(2 * PI * frequenz * t);

  // --- 2. Das Rauschen generieren ---
  // Erzeugt zufällige Werte zwischen -150 und +150
  int noise = random(-noiseLevel, noiseLevel);

  // --- 3. Signale addieren ---
  int combinedSignal = (int)cleanSine + noise;

  // --- Sicherheits-Clamping ---
  // Verhindert, dass wir den 12-Bit Bereich verlassen (0-4095)
  // Dies verhindert Überläufe, die wie Glitches aussehen würden.
  combinedSignal = constrain(combinedSignal, 0, 4095);

  // --- DAC Ausgabe (Echtzeit Signal) ---
  // Dies ist das Signal, das du mit deinem externen RC-Filter (LPF/HPF) bearbeiten kannst.
  analogWrite(dacPin, combinedSignal);

  // --- Serial Plotter Ausgabe ---
  static unsigned long lastPrint = 0;
  
  // Ausgabe alle 10ms (100 Hz Samplingrate im Plotter)
  if (millis() - lastPrint > 10) { 
      // Feste Skalierung für den Plotter, damit der Zoom nicht springt
      Serial.print("Max:4095 "); 
      Serial.print("Min:0 ");
      
      // Das "ideale" saubere Signal (zum Vergleich) in Blau/Grün
      Serial.print("Baseline:");
      Serial.print((int)cleanSine);
      Serial.print(" ");
      
      // Das verrauschte Signal (das tatsächlich am DAC anliegt) in Rot/Orange
      Serial.print("NoisyOutput:");
      Serial.println(combinedSignal); 
      
      lastPrint = millis();
  }
  
  // Kleines Delay für die Loop-Stabilität, aber kurz genug für hochfrequentes Rauschen
  delayMicroseconds(100); 
}