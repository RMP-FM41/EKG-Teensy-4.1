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
// Hier stellst du ein, wie viel Zeit auf dem Display zu sehen ist:
const float TIME_WINDOW_SEC = 3.0; // 3.0 Sekunden (bei 60 BPM sind das 3 Schläge)
const int SAMPLE_RATE_HZ = 300;    // Unsere Abtastrate

// Berechnung der benötigten Array-Größe
const int HISTORY_SIZE = (int)(TIME_WINDOW_SEC * SAMPLE_RATE_HZ); 
// Hinweis: Bei 3.0s * 300Hz = 900 Werte. Teensy 4.1 hat genug RAM dafür!

// ============ Puffer & Arrays ============
float ekgHistory[HISTORY_SIZE]; // Das große Array für die Historie

// Ring Buffer für den Interrupt (Zwischenspeicher)
const int ISR_BUFFER_SIZE = 512; 
volatile float isrBuffer[ISR_BUFFER_SIZE];
volatile int head = 0; 
volatile int tail = 0;

// ============ Timer & Variablen ============
IntervalTimer sampleTimer;
const int SCREEN_WIDTH = 320;
const int GRAPH_HEIGHT = 180;
const int GRAPH_Y_OFFSET = 40;

float minVoltage = -500;
float maxVoltage = 500;
float displayMin = -500;
float displayMax = 500;
unsigned long lastDisplayUpdate = 0;

// ============ I2C Register ============
#define ADS1115_REG_POINTER_CONVERT 0x00

// ============ INTERRUPT (Datenerfassung) ============
void sampleISR() {
  Wire.beginTransmission(0x48);
  Wire.write(ADS1115_REG_POINTER_CONVERT);
  Wire.endTransmission();
  
  Wire.requestFrom(0x48, 2);
  int16_t val = (Wire.read() << 8) | Wire.read();
  float voltage = val * 0.0625; 

  int nextHead = (head + 1) % ISR_BUFFER_SIZE;
  if (nextHead != tail) {
    isrBuffer[head] = voltage;
    head = nextHead;
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  
  // UI Header
  tft.fillRect(0, 0, SCREEN_WIDTH, 38, ILI9341_BLUE);
  tft.setCursor(10, 8);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
  tft.print("EKG: ");
  tft.print(TIME_WINDOW_SEC, 1);
  tft.print(" sek");

  Wire.begin();
  Wire.setClock(400000); 
  
  ads.begin();
  ads.setGain(GAIN_TWO);
  ads.setDataRate(RATE_ADS1115_860SPS); 
  ads.startComparator_SingleEnded(0, 1000); 

  // Array leeren
  for (int i = 0; i < HISTORY_SIZE; i++) ekgHistory[i] = 0.0;

  // Timer starten (300 Hz = 3333 us)
  sampleTimer.begin(sampleISR, 3333); 
}

void loop() {
  // 1. Daten aus dem Interrupt-Puffer holen
  while (head != tail) {
    float voltage = isrBuffer[tail];
    tail = (tail + 1) % ISR_BUFFER_SIZE;
    
    // Serial Plotter Output (Echtzeit)
    Serial.print("EKG:");
    Serial.println(voltage); 

    // In das große History-Array schieben
    // (Verschieben ist für Teensy 4.1 bei 1000 Werten kein Problem)
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
      ekgHistory[i] = ekgHistory[i + 1];
    }
    ekgHistory[HISTORY_SIZE - 1] = voltage;
    
    // Min/Max Anpassung
    if (voltage < minVoltage) minVoltage = voltage;
    if (voltage > maxVoltage) maxVoltage = voltage;
  }

  // Auto-Scale leicht zurückfahren
  minVoltage += 0.5;
  maxVoltage -= 0.5;
  
  float center = (maxVoltage + minVoltage) / 2.0;
  float range = (maxVoltage - minVoltage);
  if (range < 50) range = 50;
  displayMin = center - (range * 0.6); // Zoom fix auf 1.0 in dieser Logik
  displayMax = center + (range * 0.6);

  // 2. Display Update (30 FPS)
  if (millis() - lastDisplayUpdate > 33) {
    lastDisplayUpdate = millis();
    drawCompressedGraph();
  }
}

void drawCompressedGraph() {
  tft.fillRect(0, GRAPH_Y_OFFSET, SCREEN_WIDTH, GRAPH_HEIGHT, ILI9341_BLACK);
  
  // Nulllinie
  int zeroY = map(0, displayMin, displayMax, GRAPH_Y_OFFSET + GRAPH_HEIGHT, GRAPH_Y_OFFSET);
  if(zeroY >= GRAPH_Y_OFFSET && zeroY <= GRAPH_Y_OFFSET + GRAPH_HEIGHT)
     tft.drawFastHLine(0, zeroY, SCREEN_WIDTH, 0x39E7);

  // ============ DER TRICK: KOMPRIMIERUNG ============
  // Wir haben HISTORY_SIZE (z.B. 900) Datenpunkte, aber nur SCREEN_WIDTH (320) Pixel.
  // step berechnet, wie viele Datenpunkte wir pro Pixel überspringen müssen.
  float step = (float)HISTORY_SIZE / (float)SCREEN_WIDTH;
  
  int lastY = -1;
  
  // Wir gehen Pixel für Pixel durch (0 bis 319)
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    
    // Wir berechnen, welcher Index im großen Array diesem Pixel entspricht
    int dataIndex = (int)(x * step);
    
    // Sicherheitscheck
    if (dataIndex >= HISTORY_SIZE) dataIndex = HISTORY_SIZE - 1;

    float val = ekgHistory[dataIndex];

    int y = map(val, displayMin, displayMax, 
                GRAPH_Y_OFFSET + GRAPH_HEIGHT, GRAPH_Y_OFFSET);
    y = constrain(y, GRAPH_Y_OFFSET, GRAPH_Y_OFFSET + GRAPH_HEIGHT);
    
    if (x > 0) {
      uint16_t color = ILI9341_GREEN;
      if(abs(val) > 1500) color = ILI9341_YELLOW;
      
      tft.drawLine(x - 1, lastY, x, y, color);
    }
    lastY = y;
  }
  
  // Info-Zeile unten
  tft.setCursor(0, 225);
  tft.fillRect(0, 225, 320, 15, ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.print("Zeitfenster: ");
  tft.print(TIME_WINDOW_SEC, 1);
  tft.print("s (ca. ");
  tft.print(TIME_WINDOW_SEC * 1.0, 0); // Grobe Schätzung beats
  tft.print(" Beats @ 60BPM)");
}