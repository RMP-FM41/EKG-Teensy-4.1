#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ILI9341_t3.h>
#include <SPI.h>
#include <IntervalTimer.h>

// ============ Display Pins ============
#define TFT_DC  9
#define TFT_CS  10
#define TFT_RST 255
#define TFT_MOSI 11
#define TFT_SCLK 13
#define TFT_MISO 12

ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_MISO);

// ============ ADC ============
Adafruit_ADS1115 ads;

// ============ EINSTELLUNGEN ============
// Zeit auf dem TFT einstellen
const float TIME_WINDOW_SEC = 3.0; // 3 Sekunden -> bei 60 bpm 3 Zyklen
const int SAMPLE_RATE_HZ = 300;    // Abtastrate: 300 Hz

// Berechnung der Array-Größe, die benötigt wird
const int HISTORY_SIZE = (int)(TIME_WINDOW_SEC * SAMPLE_RATE_HZ);  // bei 3s * 300Hz = 900 Werte.

// ============ Puffer & Arrays ============
float ekgHistory[HISTORY_SIZE]; // Das große Array für die Historie

// Ring Buffer für den Interrupt; FIFO-Prinzip
const int ISR_BUFFER_SIZE = 512;      // ISR = Interrupt Service Routine
volatile float isrBuffer[ISR_BUFFER_SIZE];
volatile int head = 0; 
volatile int tail = 0;

// ============ Timer & Variablen ============
// Anzeige: Breite, Höhe, Offset
IntervalTimer sampleTimer;
const int SCREEN_WIDTH = 320;
const int GRAPH_HEIGHT = 180;
const int GRAPH_Y_OFFSET = 40;

// aktuelle min. und max x-Werte
float minVoltage = -500;
float maxVoltage = 500;

// Darstellungsgrenzen, y-Werte 
float displayMin = -500;
float displayMax = 500;
unsigned long lastDisplayUpdate = 0;

// ============ I2C Register ============
#define ADS1115_REG_POINTER_CONVERT 0x00    // setze pointer auf Register von ADS1115

// ============ INTERRUPT (Datenerfassung) ============
void sampleISR() {
  Wire.beginTransmission(0x48);     // starte Übertragung an 0x48
  Wire.write(ADS1115_REG_POINTER_CONVERT);    // setze pointer auf 0x00
  Wire.endTransmission();           // beende Pointer-Setzung
  
  // 2 byte (16 Bit) vom Pointer vom Register lesen lassen:
  Wire.requestFrom(0x48, 2);          // 2 Byte von 0X48 lesen
  int16_t val = (Wire.read() << 8) | Wire.read();     // Hi-Byte und Low-Byte zusammensetzen zu 16 Bit

// Daten in mV umrechnen:
  float voltage = val * 0.0625; 

// Wert in Ringbuffer schreiben, nächste Position im buffer wählen
  int nextHead = (head + 1) % ISR_BUFFER_SIZE;

// prüfen ob Buffer voll ist
  if (nextHead != tail) {
    isrBuffer[head] = voltage;    // Wert speichern an aktueller Head-Position
    head = nextHead;              // head-pointer weiterschieben
  }
}


// ============ Serielle Kommunikation ============
void setup() {
  Serial.begin(115200);   // initialisiere Serial mit 115200 baud

// ============ TFT DISPLAY  ============
// Initialisierung
  tft.begin();           // starte Display
  tft.setRotation(3);    // (320x240 querformat)
  tft.fillScreen(ILI9341_BLACK); // schwarzer Hintergrund
  

// Header: blaue Titelleiste oben

  tft.fillRect(0, 0, SCREEN_WIDTH, 38, ILI9341_BLUE);   // blauer Balken
  tft.setCursor(10, 8);                         // Textposition
  tft.setTextColor(ILI9341_WHITE); // weisse Schrift
  tft.setTextSize(2);  				// Größe 2
  tft.print("EKG: ");			// Titel
  tft.print(TIME_WINDOW_SEC, 1);	// Zeitfenster anzeigen
  tft.print(" sek");			// Einheit

// ============ I2C BUS INITIALISIERUNG ============
  Wire.begin();				// starte I2C Bus für ADS1115
  Wire.setClock(400000); 	// setze Taktung auf 400 kHz

// ============ ADS1115 ADC KONFIGURATION ============  
  ads.begin();		// initialisiere ADS1115 
  ads.setGain(GAIN_TWO);	//Verstärkung auf +- 2,048 V 
  ads.setDataRate(RATE_ADS1115_860SPS); 	// max. Abtastrate 860 SPS
  ads.startComparator_SingleEnded(0, 1000); 	// Starte Wandlung auf Kanal A0; threshold = 1000 ist dummy-Wert 	

// Array leeren
  for (int i = 0; i < HISTORY_SIZE; i++) ekgHistory[i] = 0.0;

// Timer-Interrupt starten (300 Hz = 3333 µs)
  sampleTimer.begin(sampleISR, 3333); 
}

void loop() {
  // 1. Daten aus dem Interrupt-Ringbuffer holen
  while (head != tail) {	// solange neue Daten im Buffer sind, hole ältesten Wert aus dem Buffer -> FIFO
    float voltage = isrBuffer[tail];
    tail = (tail + 1) % ISR_BUFFER_SIZE;	// tail-Pointer weiterbewegen
    
// Serial Plotter Output an PC
    Serial.print("EKG:");
    Serial.println(voltage); 	// Wert ohne Einheit ausgeben wegen Autoscale

// In das große History-Array schieben, dann alle Werte um 1 nach links verschieben -> Scrolling buffer 

    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
      ekgHistory[i] = ekgHistory[i + 1];	// einen Wert weiterschieben 
    }
    ekgHistory[HISTORY_SIZE - 1] = voltage;	// neuer Wert ans Ende schreiben
    
// Autoskalierung: Min / Max-Anpassung
    if (voltage < minVoltage) minVoltage = voltage;
    if (voltage > maxVoltage) maxVoltage = voltage;
  }

// Auto-Scale leicht zurückfahren
  minVoltage += 0.5;	// Min driftet langsam nach oben, 0,5 mV pro Loop
  maxVoltage -= 0.5;	// Max driftet langsam nach unten, 0,5 mV pro Loop
  
// Berechne Mittelwert und Bereich des aktuellen Signals 
  
  float center = (maxVoltage + minVoltage) / 2.0; // Zentrum y-Achse
  float range = (maxVoltage - minVoltage);		// Gesamtbereich Peak to Peak
// Mindestbereich festlegen -> verhindert Division durch 0
  if (range < 50) range = 50;	// Minimum 50 mV
  displayMin = center - (range * 0.6); // Untere Y-Achsengrenze
  displayMax = center + (range * 0.6);	// obere Y-Achsengrenze 

// 2. Display Update (30 FPS); 
  if (millis() - lastDisplayUpdate > 33) { // aktualisiert alle 33 ms
    lastDisplayUpdate = millis();	// speichere aktuellen Zeitstempel
    drawCompressedGraph();			// Zeichne kompletten Graphen neu
  }
}

// Flimmern verringern: nur Graph-Bereich löschen, nicht header
void drawCompressedGraph() {
  tft.fillRect(0, GRAPH_Y_OFFSET, SCREEN_WIDTH, GRAPH_HEIGHT, ILI9341_BLACK);
  
// Nulllinie zeichnen
  int zeroY = map(0, displayMin, displayMax, GRAPH_Y_OFFSET + GRAPH_HEIGHT, GRAPH_Y_OFFSET);
// nur zeichnen wenn sie im sichtbaren Bereich liegt
  if(zeroY >= GRAPH_Y_OFFSET && zeroY <= GRAPH_Y_OFFSET + GRAPH_HEIGHT)
     tft.drawFastHLine(0, zeroY, SCREEN_WIDTH, 0x39E7);

// Downsampling, um 900 Samples auf 320 Pixel darstellen zu können
// step berechnet: 900 / 320 ≈ 2,82 -> jeden 3. Datenpunkt nehmen 

  float step = (float)HISTORY_SIZE / (float)SCREEN_WIDTH;
  
  int lastY = -1;		// speichere Y-Koordinate des vorherigen Pixels
  
// ============  Hauptloop: Pixel für Pixel Zeichnen ============ 
  for (int x = 0; x < SCREEN_WIDTH; x++) {	// X=0 bis 319
    
// Welcher Index im ekgHistory-Array passt zu diesem Pixel? 
    int dataIndex = (int)(x * step);
    
// verhindert Array-Overflow:
    if (dataIndex >= HISTORY_SIZE) dataIndex = HISTORY_SIZE - 1;

float val = ekgHistory[dataIndex];	// hole Spannungswert aus der History


// ============ Konvertiere Spannung -> Pixel-Y-Koordinate ============ 
// map() konvertiert Spannungswert in Y-Pixelposition im Bereich von Displaybereich:
    int y = map(val, displayMin, displayMax, 
                GRAPH_Y_OFFSET + GRAPH_HEIGHT, GRAPH_Y_OFFSET);
	
// ============ Clipping vermeiden durch Grenzanpassung ============	
// wenn y-Koordinate außerhalb des Bereichs liegt, werden Top- und Bottomgrenzen angepasst
    y = constrain(y, GRAPH_Y_OFFSET, GRAPH_Y_OFFSET + GRAPH_HEIGHT);
    

//============ Linienzug Zeichnen============
// jeden Punkt mit dem vorherigen verbinden 
    if (x > 0) {

// Farbe wählen, je nach Signalamplitude
      uint16_t color = ILI9341_GREEN;		// standard: grün
      if(abs(val) > 1500) color = ILI9341_YELLOW;	// größer 1500 mV -> gelb
      
      tft.drawLine(x - 1, lastY, x, y, color); // Linie zeichnen
    }
    lastY = y;		// aktuelle Y-Position speichern 
  }
}
