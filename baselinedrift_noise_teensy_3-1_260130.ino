// Teensy 3.2 DAC Pin
const int dacPin = A14;

void setup() {
  // 12-Bit Auflösung (0-4095) für weiche Kurven
  analogWriteResolution(12);
  
  // Serial starten
  Serial.begin(115200);

  // Zufallsgenerator für noise initialisieren 
  randomSeed(analogRead(A0));
}

void loop() {
  // --- Zeitbasis ---
  float t = micros() / 1000000.0; 

  // --- Parameter für Baseline Drift ---
  float frequenz = 0.2;       // 0,2 Hz 
  float amplitude = 250.0;    // Amplitude der Sinuswelle (also insgesamt 2x 250)
  float offset = 250.0;      // Mitte höher setzen 

  // --- Parameter für Rauschen  ---

  int noiseLevel = 150; // "Amplitude" (1x 150)

  // --- 1. Baseline berechnen ---
  float cleanSine = offset + amplitude * sin(2 * PI * frequenz * t);

  // --- 2. Rauschen generieren ---
  int noise = random(-noiseLevel, noiseLevel);

  // --- 3. beide Signale addieren ---
  int combinedSignal = (int)cleanSine + noise;

  // --- Sicherheits-Clamping ---
  // verhindert, dass 12 Bit-Bereich verlassen wird -> Glitches

  combinedSignal = constrain(combinedSignal, 0, 4095);

  // --- DAC Ausgabe (Echtzeitsignal) ---
  analogWrite(dacPin, combinedSignal);

  // --- Serial Plotter Ausgabe ---
  static unsigned long lastPrint = 0;
  
  // Ausgabe alle 10ms (100 Hz Samplingrate im Plotter)
  if (millis() - lastPrint > 10) { 
      // Feste Skalierung für den Plotter, damit der Zoom nicht springt
      Serial.print("Max:4095 "); 
      Serial.print("Min:0 ");
      
      // Das "ideale" saubere Signal (zum Vergleich) in Blau/Grün (nur im plotter, nicht auf TFT)
      Serial.print("Baseline:");
      Serial.print((int)cleanSine);
      Serial.print(" ");
      
      // Das verrauschte Signal (das tatsächlich am DAC anliegt) in Rot/Orange (nur im plotter, nicht auf TFT)
      Serial.print("NoisyOutput:");
      Serial.println(combinedSignal); 
      
      lastPrint = millis();
  }
  
  // Kleines Delay für die Loop-Stabilität, aber kurz genug für hochfrequentes Rauschen
  delayMicroseconds(100); 
}