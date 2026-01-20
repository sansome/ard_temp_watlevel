
/*
  Távoli Arduino UNO – AJ-SR04M + RS485 válaszadó egység
  - Állítsd be: NODE_ID (1 vagy 2)
  - NODE_ID==1: TRIG=D3, ECHO=D4
  - NODE_ID==2: TRIG=D5, ECHO=D6
  - RS485: RO->A0 (RX), DI->A1 (TX), RE->D8, DE->D9
  - Válasz formátum: "T1:xx\n" vagy "T2:yy\n"; hiba esetén "T?:-1\n"
*/

#include <SoftwareSerial.h>

// ----------------- KONFIG -----------------
#define NODE_ID 1        // <-- Ezt állítsd: 1 az első tartály, 2 a második

// RS485 (MAX485) lábak
const byte RS485_RX_PIN = A0; // RO -> ide
const byte RS485_TX_PIN = A1; // DI -> innen
const byte RS485_RE_PIN = 8;  // RE
const byte RS485_DE_PIN = 9;  // DE

SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN); // RX, TX

// AJ-SR04M trig/echo lábak tartályonként
#if (NODE_ID == 1)
  const byte TRIG_PIN = 3;
  const byte ECHO_PIN = 4;
  const char* REQ_STR  = "T1?";
  const char* RESP_PFX = "T1:";
#elif (NODE_ID == 2)
  const byte TRIG_PIN = 5;
  const byte ECHO_PIN = 6;
  const char* REQ_STR  = "T2?";
  const char* RESP_PFX = "T2:";
#else
  #error "NODE_ID csak 1 vagy 2 lehet!"
#endif

// Tartály geometria (mm)
const int TANK_HEIGHT_MM      = 1730;
const int TOP_FULL_OFFSET_MM  = 100;              // 100%: tetőtől 100 mm
const int BOTTOM_EMPTY_OFF_MM = 100;              // 0%: aljától 100 mm
const int D_MAX_MM = TANK_HEIGHT_MM - BOTTOM_EMPTY_OFF_MM; // 1630 mm
const int D_MIN_MM = TOP_FULL_OFFSET_MM;                   // 100 mm
const int RANGE_MM = (D_MAX_MM - D_MIN_MM);                // 1530 mm

// Mérési beállítások
const unsigned long PULSE_TIMEOUT_US = 30000UL; // ~30 ms (~> 5 m)
const byte SAMPLES = 3;                         // medián szűréshez

// ----------------- SEGÉD -----------------
void rs485TxMode() {           // adás
  digitalWrite(RS485_DE_PIN, HIGH);
  digitalWrite(RS485_RE_PIN, HIGH);
}
void rs485RxMode() {           // vétel
  digitalWrite(RS485_DE_PIN, LOW);
  digitalWrite(RS485_RE_PIN, LOW);
}

long echoPulseUs(byte trigPin, byte echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // AJ-SR04M-nél stabilabb a 20 us-os trig
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);
  return pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
}

float distanceCmOnce() {
  long us = echoPulseUs(TRIG_PIN, ECHO_PIN);
  if (us <= 0) return NAN;
  return us / 58.0; // 58 us ~ 1 cm (20°C körül)
}

float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b; // középső
}

float distanceCmMedian() {
  float v1 = distanceCmOnce(); delay(50);
  float v2 = distanceCmOnce(); delay(50);
  float v3 = distanceCmOnce();
  // Ha bármelyik NaN, próbáljuk a többivel mediánt számolni
  bool ok1 = isfinite(v1), ok2 = isfinite(v2), ok3 = isfinite(v3);
  if (ok1 && ok2 && ok3) return median3(v1, v2, v3);
  if (ok1 && ok2) return (v1 + v2) / 2.0;
  if (ok1 && ok3) return (v1 + v3) / 2.0;
  if (ok2 && ok3) return (v2 + v3) / 2.0;
  return NAN;
}

int cmToPercentInt(float d_cm) {
  if (!isfinite(d_cm)) return -1;
  long d_mm = lround(d_cm * 10.0);

  // clamp a használható ablakra
  if (d_mm < D_MIN_MM) d_mm = D_MIN_MM;
  if (d_mm > D_MAX_MM) d_mm = D_MAX_MM;

  float pct = (float)(D_MAX_MM - d_mm) * 100.0f / (float)RANGE_MM;
  int p = (int)lround(pct);
  if (p < 0) p = 0; if (p > 100) p = 100;
  return p;
}

// ----------------- SETUP/LOOP -----------------
void setup() {
  pinMode(RS485_RE_PIN, OUTPUT);
  pinMode(RS485_DE_PIN, OUTPUT);
  rs485RxMode();

  rs485.begin(9600);   // egyezzen a központival

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Opcionális: egy rövid késleltetés induláskor
  delay(200);
}

void loop() {
  // Várunk egy bejövő sort (kérés) – pl. "T1?\n" / "T2?\n"
  if (rs485.available()) {
    String req = rs485.readStringUntil('\n');
    req.trim();

    if (req.equals(REQ_STR)) {
      // Mérünk
      float d_cm = distanceCmMedian();
      int pct = cmToPercentInt(d_cm);

      // Válasz felépítése
      String resp(RESP_PFX);
      resp += String(pct);  // ha hiba: -1
      resp += "\n";

      // Kiküldés
      rs485TxMode();
      delay(2);
      rs485.print(resp);
      rs485.flush();
      delay(2);
      rs485RxMode();
    }
    // Ha nem a mi kérésünk (pl. a másik csomóponthoz ment), ignoráljuk.
  }
}
``
