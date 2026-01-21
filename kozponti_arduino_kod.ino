
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <Adafruit_INA219.h>

// ============================================================================
// HARDVER / OBJEKTUMOK
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// INA219 (I2C cím módosítható, ha nem 0x40)
const uint8_t INA219_ADDR = 0x40;
Adafruit_INA219 ina219(INA219_ADDR);

// Gomb
const byte BUTTON_PIN = 2;

// DS18B20
const byte DS_PWR_PIN = 7;
const byte ONE_WIRE_BUS = 8;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// RS485 (MAX485)
const byte DE_PIN = 10;
const byte RE_PIN = 11;
HardwareSerial &rs = Serial;

// Relé a távoli 12V-hoz
const byte RELAY_PIN = 12;
const bool RELAY_ACTIVE_LOW = true;

// Idők
const unsigned long AWAKE_MS = 10000UL;

volatile bool wakeFlag = false;

// ============================================================================
// CUSTOM CHARACTERS (0: hőmérő, 1: vízcsepp, 2: akkumulátor)
// ============================================================================
byte thermometerThin[8] = {
  B00100,
  B01010,
  B01010,
  B01110,
  B01110,
  B11111,
  B11111,
  B01110
};

byte droplet[8] = {
  B01110,
  B11011,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B11111
};

byte batteryIcon[8] = {
  B00000,
  B00000,
  B01010,
  B11111,
  B10001,
  B10001,
  B10001,
  B11111
};

// ============================================================================
// INA219 SLEEP/WAKE – regiszter szintű kezelés (MODE bit 2..0)
// ============================================================================
uint16_t ina219_readConfig()
{
  Wire.beginTransmission(INA219_ADDR);
  Wire.write(0x00);                // Config regiszter címe
  Wire.endTransmission(false);     // repeated start
  Wire.requestFrom((int)INA219_ADDR, 2);
  if (Wire.available() < 2) return 0xFFFF;
  uint16_t msb = Wire.read();
  uint16_t lsb = Wire.read();
  return (msb << 8) | lsb;
}

void ina219_writeConfig(uint16_t cfg)
{
  Wire.beginTransmission(INA219_ADDR);
  Wire.write(0x00);                // Config regiszter
  Wire.write((uint8_t)(cfg >> 8)); // MSB
  Wire.write((uint8_t)(cfg & 0xFF));// LSB
  Wire.endTransmission();
}

void ina219_sleep()
{
  uint16_t cfg = ina219_readConfig();
  if (cfg == 0xFFFF) return;       // I2C hiba esetén hagyjuk békén
  cfg &= ~0x0007;                  // MODE[2:0] = 000 (Power-Down)
  ina219_writeConfig(cfg);
}

void ina219_wakeup()
{
  uint16_t cfg = ina219_readConfig();
  if (cfg == 0xFFFF) return;
  cfg = (cfg & ~0x0007) | 0x0007;  // MODE[2:0] = 111 (Shunt+Bus continuous)
  ina219_writeConfig(cfg);
}

// ============================================================================
// SEGÉDFÜGGVÉNYEK
// ============================================================================
void wakeISR() { wakeFlag = true; }

void waitButtonReleased(uint16_t stableMs=30, uint16_t timeoutMs=3000) {
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
  rs485Tx(); delay(1);
  rs.print(msg);
  rs.flush(); delay(1);
  rs485Rx();
}

String rs485Receive(uint16_t timeout=350) {
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
  String resp = rs485Receive();
  if (resp.startsWith("T1:") || resp.startsWith("T2:")) {
    int v = resp.substring(3).toInt();
    if (v >= 0 && v <= 100) return v;
  }
  return -1;
}

void goToSleep() {
  lcd.clear(); lcd.noBacklight();

  digitalWrite(DS_PWR_PIN, LOW);
  pinMode(ONE_WIRE_BUS, INPUT);

  relaySet(false);

  // INA219 is aludjon (ha épp fenn maradt volna)
  ina219_sleep();

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
  delay(30);
}

// ============================================================================
// SETUP
// ============================================================================
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
  lcd.createChar(0, thermometerThin);
  lcd.createChar(1, droplet);
  lcd.createChar(2, batteryIcon);

  // INA219 indul (majd azonnal altatjuk, hogy alvó ciklusban is takarékos legyen)
  ina219.begin();
  ina219_sleep();

  EIFR |= (1 << INTF0);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  // 1) Alvás
  goToSleep();

  // 2) Távoli állomások táp ON
  relaySet(true);
  delay(320); // UNO boot ~<300 ms

  // 3) INA219 ébresztése – minél előbb, hogy a buszkonverziók futhassanak
  ina219_wakeup();

  // 4) DS18B20: táp ON + nem blokkoló konverzió
  digitalWrite(DS_PWR_PIN, HIGH);
  delay(10);
  sensors.begin();
  sensors.setResolution(10);            // ~187.5 ms
  sensors.setWaitForConversion(false);  // aszinkron
  sensors.requestTemperatures();

  // 5) LCD "Meres..." állapot
  //lcd.backlight();
  //lcd.clear();

  // 1. sor: hőmérő ikon + space + "Meres..."
  //lcd.setCursor(0,0); lcd.write(byte(0)); lcd.print(' ');
  //lcd.setCursor(2,0); lcd.print("Meres...");

  // 2. sor: vízcsepp ikon + space + helykitöltő
  //lcd.setCursor(0,1); lcd.write(byte(1)); lcd.print(' ');
  //lcd.setCursor(2,1); lcd.print("T1:--% T2:--%");

  // 6) RS485 lekérések (közben INA219 folyamatos és DS18B20 konvertál)
  int t1 = requestTankLevel(1);
  int t2 = requestTankLevel(2);

  // 7) Hőmérséklet beolvasás (ekkorra kész)
  float tempC = sensors.getTempCByIndex(0);

  // 8) Akkufeszültség (INA219 Bus Voltage) + százalék számítás
  //    10.0 V → 0%, 14.6 V → 100%
  float busV = ina219.getBusVoltage_V();
  float pct  = (busV - 10.0f) * (100.0f / (14.6f - 10.0f));
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // 9) LCD frissítés: 1. sor hőmérséklet + akku%, 2. sor vízszintek
  lcd.clear();
  lcd.backlight();

  // 1. sor
  lcd.setCursor(0,0); lcd.write(byte(0)); lcd.print(' ');
  lcd.setCursor(2,0);
  if (isnan(tempC) || tempC <= -55 || tempC >= 125) {
    lcd.print("Temp hiba");
  } else {
    lcd.print(tempC, 1); lcd.print((char)223); lcd.print("C ");
  }
  // akku ikon + % (az 1. sor végére)
  lcd.write(byte(2)); lcd.print(' ');
  lcd.print((int)pct); lcd.print("%");

  // 2. sor
  lcd.setCursor(0,1); lcd.write(byte(1)); lcd.print(' ');
  lcd.setCursor(2,1);
  lcd.print("1:");
  if (t1 < 0) lcd.print("--%");
  else { lcd.print(t1); lcd.print("%"); }
  lcd.print(" 2:");
  if (t2 < 0) lcd.print("--%");
  else { lcd.print(t2); lcd.print("%"); }

  // 10) 10 mp kijelzés
  delay(AWAKE_MS);

  // 11) Leállítások: DS18B20 OFF, INA219 sleep, relé OFF, LCD OFF
  digitalWrite(DS_PWR_PIN, LOW);
  pinMode(ONE_WIRE_BUS, INPUT);

  ina219_sleep();       // <<< fontos: alvás a következő ciklusig
  relaySet(false);

  lcd.clear();
  lcd.noBacklight();
}
``
