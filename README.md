# Bekötés - központi arduino
1) Ébresztő gomb
A membrán gomb két kivezetésű.
GOMB egyik lába → D2
GOMB másik lába → GND

A kód INPUT_PULLUP‑ot használ → gombnyomáskor FALLING.

2) DS18B20 hőmérséklet szenzor (háromvezetékes)
A szenzort külön D7 lábról tápláljuk, hogy alvó módban kikapcsoljon.
DS18B20 VDD   → D7
DS18B20 GND   → GND
DS18B20 DATA  → D8

4.7kΩ felhúzó ellenállás: D8 ↔ D7

‼️ Nagyon fontos, hogy az ellenállás NE 5V-ról, hanem D7-ről menjen, különben a szenzor nem alszik ki teljesen.

3) I2C LCD 16×2 kijelző
(Most 0x27 című I2C panelt feltételezünk.)
LCD VCC → 5V
LCD GND → GND
LCD SDA → A4
LCD SCL → A5

4) Relémodul (a távoli Arduinók 12 V tápjának vezérléséhez)
Arduino oldala:
Relé IN  → D12
Relé VCC → 5V
Relé GND → GND

Terhelési oldal (12 V kapcsolása):
12V TÁP + → Relé COM
Relé NO → 20 méteres kábel → Távoli Arduino VIN

12V TÁP − → központi Arduino GND → távoli Arduino GND

NO (Normally Open) = csak akkor kap tápot a távoli Arduino, amikor a központi bekapcsolja a relét.

5) RS485 – MAX485 modul (központi oldalon)
A központi Arduino RS485-ön beszél a két távoli Arduinóval.
MAX485 → Központi Arduino
RO  → D0 (Arduino RX)
DI  → D1 (Arduino TX)
RE  → D11
DE  → D10

VCC → 5V
GND → GND

MAX485 → RS485 busz
A → RS485 kábel A ér
B → RS485 kábel B ér

A busz két végére (központi + legtávolabbi távoli modulhoz):
A–B közé: 120 Ω lezáró ellenállás

‼️ Az RS485 buszon csak az A/B vezeték fut, sem 5V, sem egyéb jel nem mehet hosszú távra.

6) Teljes közös GND kötelező
A következő pontok GND-jét ÖSSZE KELL kötni:

központi Arduino GND
relémodul GND
MAX485 modul GND
DS18B20 GND
távoli Arduinók GND (20 m kábelen visszajön)

Ez a kommunikáció és a relé miatt kötelező.

🟩 Összefoglaló blokkdiagram
                +-------------------+
                |   Központi UNO    |
                |-------------------|
   GOMB → D2 ---|                   |--- D7 → DS18B20 VDD
                |                   |--- D8 → DS18B20 DATA
                |                   |
                |                   |--- A4 → LCD SDA
                |                   |--- A5 → LCD SCL
                |                   |
                |                   |--- D12 → Relé IN
                |                   |
                |                   |--- D10 → MAX485 DE
                |                   |--- D11 → MAX485 RE
                |                   |--- D0 → MAX485 RO
                |                   |--- D1 → MAX485 DI
                +-------------------+
                           |
                       RS485 A/B
                           |
           -------------------------------------
           |                                   |
   Távoli Arduino #1                    Távoli Arduino #2
   (külön 12V tápot kap)               (külön 12V tápot kap)

És a relén átmenő 12 V látja el mindkét távoli Arduinót:
12V + → Relé COM
Relé NO → 20 m → Távoli UNO VIN (+szenzor)
12V – → közös GND → vissza


# Bekötés – távoli állomás (mindkét tartály ugyanígy)
Táp:
- A központi relé 12 V‑ot küld a távoli állomásra → távoli Arduino VIN (vagy DC‑jack)
- GND közös a központival

AJ‑SR04M (helyben, rövid kábellel):

NODE_ID 1: TRIG → D3, ECHO → D4
NODE_ID 2: TRIG → D5, ECHO → D6
VCC → 5V, GND → GND

RS485 (MAX485 modul):

RO → Arduino A0 (RX)
DI → Arduino A1 (TX)
RE → Arduino D8
DE → Arduino D9
VCC → 5V, GND → GND
A/B → közös RS485 busz (párhuzamosan a központival és a másik távolival)

A busz KÉT végére tegyél 120 Ω lezárást (központi és a legtávolabbi modul).

# Relé bekötés
Relé modul → Arduino UNO (központi)
Relé pin  Arduino pin
IN        D12 ← a vezérlő jel innen jön
VCC       5V
GND       GND
🔋 12 V terhelési oldal (amit a relé kapcsol)
(a távoli Arduinók tápjának kapcsolása)
Relé érintkező  Funkció
COM      Bemenet a 12 V tápból
NO       Kimenet a 12 V felé (20 m kábel a távoli ardunók felé)


# kábelezés
🟦 1) 12 V TÁPKÁBEL (Központi Arduino → Relé → Távoli állomások)
Ez a legfontosabb, mert itt folyik a legnagyobb áram, 20 méteres vezetékhosszon.
Áramfelvétel becslés:

Távoli Arduino UNO ~ 70–90 mA
AJ‑SR04M szenzor ~ 30 mA csúcs
MAX485 modul ~ 5–15 mA
Egy távoli állomás összesen: ~120–150 mA
Két állomás együtt: ~240–300 mA

Számoljunk: 0.3 A (300 mA) maximummal a 12 V tápágban.
🔧 20 méterre JELENTŐS feszültségesés lehet → ezért:
✔ Ajánlott keresztmetszet: 0.75 mm² rézvezeték
(Ennél kisebb NEM javasolt.)
Ez lehet:

0.75 mm² ikerhuzal
vagy 2×0.75 mm² riasztókábel,
vagy hangszórókábel,
vagy MT kábel 2×0.75 mm².

Miért nem elég 0.5 mm²?
20 méteren a veszteség nagyobb lenne, és a 12 V a távoli Arduinóknál leesne akár 10–11 V körülre → instabil működés.
Miért nem kell 1.0 mm²?
Mert 0.75 mm² bőven elég 300 mA-re még 20 m távon is.

🟦 2) RS485 adatkábel (A/B vonalak)
Az RS485 differenciális jel kifejezetten hosszú távra van tervezve, de csak sodrott érpárat használjunk.
✔ Ajánlott kábel:
UTP CAT5 / CAT5e / CAT6 kábel, 1 sodrott érpár
Használat:

narancs + narancs-fehér → RS485 A/B

narancs: A
narancs-fehér: B

UTP előnyei:

sodrott érpár → kevesebb zaj
kis átmérő → könnyen szerelhető
RS485-nek ipari ajánlás CAT5 kábel

Keresztmetszet:

UTP 24 AWG → 0.2 mm² körül
Ez tökéletes RS485 adatjelhez.

⚠ Az RS485 A/B vezetéket NE futtasd egy kötegben a 12 V táppal, ha lehet:
minimum 5–10 cm elválasztás, külön nyomvonal ajánlott.

🟦3) Rövid lokális kábelek (távoli Arduino → AJ‑SR04M)
A szenzor helyi bekötésénél (max 1 m):
✔ Ajánlott: 0.22 mm² – 0.5 mm²
(pl. riasztókábel vagy érpáros UTP darab)
Mert:

kis áram → kis veszteség
kényelmesen szerelhető
nincs hosszú távon jelveszteség (a TRIG/ECHO itt nagyon rövid szakasz)

🟦 4) Gomb, DS18B20, LCD helyi kábelezése (központi Arduino mellett)
Minden kezelő elem a központi Arduino közelében van.
✔ Ajánlott: 0.22 mm² (szervóvezeték, Dupont kábel, riasztókábel)
Rövid távokon bőven elég.

🟦 5) Közös GND vezeték vissza a távoli Arduinóktól
Ez nagyon fontos, mert RS485 kommunikáció így stabil.
✔ Ajánlott: 0.5–0.75 mm²
Ti. ez a 12 V negatívja is egyben.
