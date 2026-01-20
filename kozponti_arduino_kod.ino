
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <avr/sleep.h>
#include <avr/power.h>

// ============================================================================
// HARDVER BEÁLLÍTÁSOK
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte BUTTON_PIN = 2;   // membrán gomb (D2-GND), INPUT_PULLUP
const byte DS_PWR_PIN = 7;   // DS18B20 táp vezérlése
const byte ONE_WIRE_BUS = 8; // DS18B20 DATA
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// RS485 (MAX485)
const byte DE_PIN = 10;      // DE
const byte RE_PIN = 11;      // RE
HardwareSerial &rs = Serial;

// Relé (12V a távoli Arduinóknak)
const byte RELAY_PIN = 12;
const bool RELAY_ACTIVE_LOW = true; // ha fordítva működik, állítsd false-ra

const unsigned long AWAKE_MS = 10000UL;

volatile bool wakeFlag = false;

// --- CUSTOM CHARS -----------------------------------------------------------
// 0: hőmérő (1. sor elejére)
byte thermometer[8] = {
  B00100,
  B01010,
  B01010,
  B01010,
  B01110,
  B11111,
  B11111,
  B01110
};
// 1: vízcsepp (2. sor elejére)
byte droplet[8] = {
  B01110,
  B11011,
  B10001,
  B10001,
  B11111,
  B11111,
  B11111,
  B11111
};
// ---------------------------------------------------------------------------

void wakeISR() { wakeFlag = true; }

// Dupla ébredés elleni gomb-felengedés
void waitButtonReleased(uint16_t stableMs = 30, uint16_t timeoutMs = 3000) {
  unsigned long start = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - start > timeoutMs) break;
    delay(1);
  }
  unsigned long t0 = millis();
  while (millis() - t0 < stableMs) {
    if (digitalRead(BUTTON_PIN) == LOW) t0 = millis();
    delay(1);
  }
}

void relaySet(bool on) {
  pinMode(RELAY_PIN, OUTPUT);
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else                  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

void rs485Tx() { digitalWrite(DE_PIN, HIGH); digitalWrite(RE_PIN, HIGH); }
void rs485Rx() { digitalWrite(DE_PIN, LOW);  digitalWrite(RE_PIN, LOW);  }

void rs485Send(const String &msg) {
  rs485Tx(); delay(2);
  rs.print(msg);
  rs.flush(); delay(2);
  rs485Rx();
}

String rs485Receive(uint16_t timeout = 600) {
  String msg = ""; unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    if (rs.available()) {
      char c = rs.read();
      if (c == '\n') break;
      msg += c;
    }
  }
  return msg;
}

int requestTankLevel(uint8_t id) {
  String cmd = (id == 1) ? "T1?\n" : "T2?\n";
  rs485Send(cmd);
  String resp = rs485Receive(800);
  if (resp.startsWith("T1:") || resp.startsWith("T2:")) {
    int v = resp.substring(3).toInt();
    if (v >= 0 && v <= 100) return v;
  }
  return -1; // hiba
}

void goToSleep() {
  lcd.clear(); lcd.noBacklight();

  digitalWrite(DS_PWR_PIN, LOW);
  pinMode(ONE_WIRE_BUS, INPUT);

  relaySet(false); // távoli állomások OFF

  ADCSRA &= ~(1 << ADEN);

  waitButtonReleased();
  EIFR |= (1 << INTF0);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), wakeISR, FALLING);
  sleep_cpu();

  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  sleep_disable();
  ADCSRA |= (1 << ADEN);
  delay(50);
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(DS_PWR_PIN, OUTPUT);
  digitalWrite(DS_PWR_PIN, LOW);

  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  rs485Rx();
  rs.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  relaySet(false);

  lcd.init();
  lcd.noBacklight();
  lcd.clear();

  // Regisztráljuk a custom karaktereket
  lcd.createChar(0, thermometer);
  lcd.createChar(1, droplet);

  EIFR |= (1 << INTF0);
}

void loop() {
  // 1) Alvás → gombnyomásra ébred
  goToSleep();

  // 2) Távoli állomások táp ON (relé)
  relaySet(true);
  delay(500); // távoli UNO boot

  // 3) Hőmérséklet
  digitalWrite(DS_PWR_PIN, HIGH);
  delay(50);
  sensors.begin();
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  // 4) Tartályok lekérése RS485-en
  int t1 = requestTankLevel(1);
  int t2 = requestTankLevel(2);

  // 5) LCD kiírás — mindkét sor elején custom ikon
  lcd.backlight();
  lcd.clear();

  // 1. sor: hőmérő ikon + hőfok
  lcd.setCursor(0, 0); lcd.write(byte(0));   // thermometer
  lcd.setCursor(1, 0);
  if (tempC < -55 || tempC > 125) {
    lcd.print("Temp hiba");
  } else {
    lcd.print(tempC, 1);
    lcd.print((char)223); // fokjel
    lcd.print("C");
  }

  // 2. sor: vízcsepp ikon + T1/T2 százalékok
  lcd.setCursor(0, 1); lcd.write(byte(1));   // droplet
  lcd.setCursor(1, 1);
  lcd.print("T1:");
  if (t1 < 0) lcd.print("--%");
  else { lcd.print(t1); lcd.print("%"); }

  lcd.print(" T2:");
  if (t2 < 0) lcd.print("--%");
  else { lcd.print(t2); lcd.print("%"); }

  // 6) 10 mp kijelzés
  delay(AWAKE_MS);

  // 7) Leállítás és vissza alvásba
  digitalWrite(DS_PWR_PIN, LOW);
  pinMode(ONE_WIRE_BUS, INPUT);
  relaySet(false);

  lcd.clear();
  lcd.noBacklight();
}
