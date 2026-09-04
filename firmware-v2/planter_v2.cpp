// ─────────────────────────────────────────────────────────────────────────────
//  Planter v2 firmware — allotment PCB build
//
//  Implements the power-design.md / power-schematic.svg decisions. This is the
//  "second sketch": it is NOT the same wiring as src/main.cpp (v1). Do not flash
//  it onto a v1 hand-wired board — the peripheral-power logic is INVERTED.
//
//  This source now supports both the original v2 XIAO ESP32-S3 proposal and the
//  Rev A XIAO ESP32-C6 carrier. Build the C6 variant with platformio-c6.ini;
//  PLANTER_XIAO_C6 is set there and selects the C6 carrier pin map below.
//
//  To build: this file replaces src/main.cpp for v2 hardware. Either move it to
//  src/ (and move the v1 main.cpp out of src/ so PlatformIO doesn't compile two
//  setup()/loop()s), or point src_dir at firmware-v2/. It expects the existing
//  src/secrets.h (WIFI_SSID, WIFI_PASSWORD, MQTT_HOST, MQTT_PORT, PLANT_ID).
//
//  What changed from v1 (traceability to power-design.md):
//   §4  Sensor VCC *and* panel VCC now hang off one switched rail (3V3_SW) gated
//       by Q1 (AO3401 P-FET) on GPIO6. Gate LOW = rail ON  (v1 was HIGH = on).
//   §4  Before sleep, every 3V3_SW-side signal line is driven LOW and held, so
//       nothing back-powers the dead rail through a peripheral's ESD diodes.
//   §5  Divider tap cap (C1, 100 nF) is now fitted on the PCB. First-sample
//       discard is kept anyway — it costs nothing and helps on a cold ADC.
//   §6  Brownout-conservation logic ported: read VBAT before WiFi, conserve
//       below 3.50 V with hysteresis to 3.65 V, treat ANY brownout reset as low
//       battery, and keep NVS post-mortem counters (no serial console in field).
//   §7  Radio stays event-driven: alert or daily heartbeat only, never on a
//       low-battery reading (that path is the brownout spiral §6 warns about).
//   §8  Panel is B/W (GDEH0154D67) — the standing choice, and we stock spares.
//       A tri-colour module was fitted as a stopgap after a panel was lost to
//       ribbon damage; that is history, not the build. The redraw stays
//       conditional — skip it when the image wouldn't change — but at a 2.6 s
//       refresh that is an optimisation, not the budget-saver it was at 14 s.
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_system.h>       // esp_reset_reason()
#include <Preferences.h>      // NVS diagnostics (survive cell disconnect)
#include <WiFi.h>
#include <PubSubClient.h>

// ── Panel selection ──────────────────────────────────────────────
// Fitted panel is the B/W GDEH0154D67 (SSD1681) — the default. The tri-colour
// options are kept for compatibility only; nothing ships on one.
//
// All three are 200x200 and share the same 8-pin interface, so the wiring and
// every layout coordinate in this file are unchanged; only the driver class,
// the colour depth and the refresh cost differ.
//
//   0 = B/W   GDEH0154D67 (SSD1681) — fitted, default,            2.6 s refresh
//   1 = B/W/R GDEH0154Z90 (SSD1682) — Waveshare 1.54" (B) V2,      14 s refresh
//   2 = B/W/R GDEW0154Z04 (IL0376F) — older Waveshare 1.54" (B),  7.5 s refresh
//
// If you do fit a tri-colour module and it shows nothing, or garbage, on
// PANEL 1, try PANEL 2. The two tri-colour modules are pin- and size-identical
// but use different controllers, and Waveshare has shipped both under the same
// product name.
// Override without editing this file: build_flags = -DPANEL=1 in platformio.ini.
#ifndef PANEL
  #define PANEL 0
#endif

#if PANEL == 0
  #include <GxEPD2_BW.h>
  #define EPD_CLASS   GxEPD2_BW
  #define EPD_DRIVER  GxEPD2_154_D67
  // NOT GxEPD_RED: GxEPD2_BW::drawPixel treats any non-zero colour as WHITE, so
  // accent elements would silently disappear rather than fall back to black.
  #define EPD_ACCENT  GxEPD_BLACK
#elif PANEL == 1
  #include <GxEPD2_3C.h>
  #define EPD_CLASS   GxEPD2_3C
  #define EPD_DRIVER  GxEPD2_154_Z90c
  #define EPD_ACCENT  GxEPD_RED
#elif PANEL == 2
  #include <GxEPD2_3C.h>
  #define EPD_CLASS   GxEPD2_3C
  #define EPD_DRIVER  GxEPD2_154c
  #define EPD_ACCENT  GxEPD_RED
#else
  #error "PANEL must be 0 (B/W D67), 1 (B/W/R Z90c) or 2 (B/W/R 154c)"
#endif

#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "secrets.h"

// ── Board pin definitions ────────────────────────────────────────
// Both carriers use one active-low high-side P-FET for 3V3_SW. The C6 map is
// deliberately expressed as GPIO numbers, matching Arduino's XIAO C6 variant.
#if defined(PLANTER_XIAO_C6)
  #define MOISTURE_PIN  1   // D1/A1 → GPIO1 (ADC1_CH1)
  #define PERIPH_EN     2   // D2/A2 → GPIO2; LOW = 3V3_SW ON
  #define EPD_CS       21   // D3
  #define EPD_DC       22   // D4
  #define EPD_RST      23   // D5
  #define EPD_BUSY     16   // D6 (panel output → our input)
  #define EPD_SCLK     19   // D8, hardware SPI SCK
  #define EPD_MOSI     18   // D10, hardware SPI MOSI
  #define BATT_PIN      0   // D0/A0 → GPIO0 (ADC1_CH0)
  #define BATT_DIV   2.0f   // 200k external top + 200k onboard bottom
#else
  // Original v2 XIAO ESP32-S3 proposal.
  #define MOISTURE_PIN  1   // D0/A0 → GPIO1 (ADC1)
  #define PERIPH_EN     6   // D5 → GPIO6; LOW = 3V3_SW ON
  #define EPD_CS        2   // D1
  #define EPD_DC        3   // D2
  #define EPD_RST       4   // D3
  #define EPD_BUSY      5   // D4 (panel output → our input)
  #define EPD_SCLK      7   // D8, hardware SPI SCK
  #define EPD_MOSI      9   // D10, hardware SPI MOSI
  #define BATT_PIN      8   // D9 / GPIO8, ADC1_CH7
  #define BATT_DIV   2.0f   // equal 220k/220k divider
#endif

// ── Calibration ──────────────────────────────────────────────────
// ⚠ v2 RECAL REQUIRED. On v1 the sensor was fed from a GPIO output (~40 Ω Rout,
// so VCC sat ~200 mV below 3V3 at ~5 mA). On v2 it gets the full rail through Q1
// (~mΩ), so VCC is ~200 mV HIGHER. The sensor is ratiometric, so both endpoints
// shift up together. These v1 numbers WILL read ~6% off until re-measured with
// DEBUG_MODE on a v2 board (raw in dry air → DRY, raw submerged → WET).
#define MOISTURE_DRY  3160  // raw ADC bone-dry → 0%   (RE-MEASURE on v2)
#define MOISTURE_WET  1300  // raw ADC submerged → 100% (RE-MEASURE on v2)

// ── Sleep ────────────────────────────────────────────────────────
#define SLEEP_MINUTES          30    // normal duty cycle
#define SLEEP_CONSERVE_MINUTES 360   // low-battery: long naps (6 h)

// ── Battery monitor (1S LiPo, 1:2 divider + fitted 100 nF tap cap) ──
// Two distinct battery ideas, do not conflate them:
//   SUSPECT (3.60): LDO nears dropout → moisture reading is untrustworthy.
//                   Informational only: flags the payload, suppresses moisture
//                   alerts. Does NOT trigger the radio.
//   CONSERVE (3.50)/RESUME (3.65): the §6 conservation band. Below CONSERVE the
//                   node skips WiFi entirely and naps long; it only resumes
//                   normal operation once back above RESUME (hysteresis).
#define BATT_TRIM     1.0f    // single-point DMM trim: BATT_TRIM = V_dmm / V_reported
#define BATT_SUSPECT  3.60f   // moisture suspect at/below (LDO dropout)
#define BATT_CONSERVE 3.50f   // enter conservation: no radio, long naps
#define BATT_RESUME   3.65f   // exit conservation (hysteresis vs CONSERVE)
#define BATT_VALID    2.50f   // below this the pin reads as unwired/floating:
                              // ignore for state (no conserve, show "--") so an
                              // unconfirmed divider can't false-trip anything.

// ── Alert thresholds ────────────────────────────────────────────
#define ALERT_DRY     20   // below this → alert
#define ALERT_WET     90   // above this → alert
#define HEARTBEAT_INTERVAL 48  // wakes between heartbeats (48 × 30 min ≈ 24 h)

// ── MQTT ────────────────────────────────────────────────────────
#define MQTT_CLIENT_ID    "planter-" PLANT_ID

// ── DEBUG MODE ───────────────────────────────────────────────────
// true  → stays awake, prints raw ADC + battery every 3 s (use for v2 recal
//         and BATT_TRIM). false → normal deep-sleep operation.
#define DEBUG_MODE false

// ── Redraw policy (§8) ───────────────────────────────────────────
// A B/W refresh costs ~2.6 s of panel + MCU activity (~0.03 mAh). E-paper holds
// its image with the rail gated off, so the cheapest refresh is the one we
// don't do: skip it when nothing a human would notice has changed.
// Forced on the daily heartbeat regardless, so the panel gets one clean full
// cycle every 24 h (keeps ghosting from building up) and a stuck reading can
// never masquerade as a fresh one for longer than a day.
//
// This saves ~1.2 mAh/day of a ~1.4 mAh/day budget — worth having, but not
// load-bearing. It was load-bearing on the tri-colour part, where 14 s on every
// wake came to 7.7 mAh/day and dwarfed the rest of the design.
// Set false to go back to redrawing on every wake; on B/W that is a safe
// diagnostic step rather than a budget emergency.
#define REDRAW_ONLY_ON_CHANGE true
#define REDRAW_DELTA          3     // percentage points

// ── RTC memory (survives deep sleep, lost on power removal) ──────
RTC_DATA_ATTR int  wakeCount  = 0;
RTC_DATA_ATTR bool conserving = false;   // latched conservation state (hysteresis)
RTC_DATA_ATTR int  lastDrawnPct        = -1000;  // sentinel: nothing drawn yet
RTC_DATA_ATTR bool lastDrawnAlert      = false;
RTC_DATA_ATTR bool lastDrawnSuspect    = false;
RTC_DATA_ATTR bool lastDrawnConserving = false;

// ── NVS diagnostics (survive a cell disconnect; the only field post-mortem) ──
Preferences prefs;
uint32_t g_brownouts = 0;
uint32_t g_minVbatMv = 9999;

// ── Display setup ────────────────────────────────────────────────
// Full-height page buffer: 200x200 mono = 5 000 B per plane. GxEPD2_3C keeps
// two planes (black + red) = 10 kB, trivial on the S3, so render in one page.
EPD_CLASS<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ── WiFi + MQTT clients ─────────────────────────────────────────
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// The signal lines that live on the switched rail and must be parked LOW + held
// before sleep (BUSY handled separately as an input).
static const gpio_num_t SW_SIGNAL_LINES[] = {
    (gpio_num_t)EPD_CS, (gpio_num_t)EPD_DC, (gpio_num_t)EPD_RST,
    (gpio_num_t)EPD_SCLK, (gpio_num_t)EPD_MOSI
};

// ── Peripheral rail control (Q1 / 3V3_SW) ────────────────────────
void enablePeripheralRail() {
    pinMode(PERIPH_EN, OUTPUT);
    digitalWrite(PERIPH_EN, LOW);   // gate LOW = rail ON
    delay(100);                     // let VCC settle before any read/init
}

// Park every rail-side signal line LOW *while the rail is still up*, so none of
// them can source current into the peripherals once VCC is gated. A line left
// HIGH would back-power the dead rail through the part's ESD diodes. Also worth
// calling early on a wake that skips the redraw: it stops CS/SCLK/MOSI sitting
// floating against a freshly powered panel for the length of a sensor read.
void parkPanelLines() {
    for (gpio_num_t p : SW_SIGNAL_LINES) {
        pinMode(p, OUTPUT);
        digitalWrite(p, LOW);
    }
}

void disablePeripheralRail() {
    parkPanelLines();
    // BUSY is a panel output → our input. With the panel unpowered it floats;
    // pull it down so the S3 input buffer doesn't sit mid-rail drawing
    // shoot-through. (Beyond the §4 list, but closes a real µA leak.)
    pinMode(EPD_BUSY, INPUT_PULLDOWN);

    // Gate the rail OFF. R1 also holds this HIGH once GPIO6 floats in sleep,
    // so GPIO6 itself is deliberately NOT hold-latched.
    digitalWrite(PERIPH_EN, HIGH);

    // Latch the parked levels so they survive deep sleep.
    for (gpio_num_t p : SW_SIGNAL_LINES) gpio_hold_en(p);
    gpio_hold_en((gpio_num_t)EPD_BUSY);
}

void releaseHolds() {
    for (gpio_num_t p : SW_SIGNAL_LINES) gpio_hold_dis(p);
    gpio_hold_dis((gpio_num_t)EPD_BUSY);
    // ESP32-S3 needs the legacy global deep-sleep hold gate in addition to
    // each pad's hold bit. ESP32-C6 supports individual pad hold directly in
    // deep sleep and its current ESP-IDF does not expose that global API.
    #if !defined(PLANTER_XIAO_C6)
      gpio_deep_sleep_hold_dis();
    #endif
}

// ── Read moisture sensor (rail must already be ON) ───────────────
int readMoisturePercent() {
    long sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogRead(MOISTURE_PIN);
        delay(10);
    }
    int raw = sum / 16;
    Serial.printf("Moisture raw ADC: %d\n", raw);

    int pct = map(raw, MOISTURE_DRY, MOISTURE_WET, 0, 100);
    return constrain(pct, 0, 100);
}

// ── Read battery voltage (divider is on B+, independent of the rail) ──
// C1 (100 nF) is fitted on v2, but discarding the first sample is free and still
// helps a cold ADC settle against the ~110 kΩ source impedance.
float readBatteryVolts() {
    analogReadResolution(12);
    analogReadMilliVolts(BATT_PIN);            // discard first
    uint32_t mv = 0;
    for (int i = 0; i < 16; i++) {
        mv += analogReadMilliVolts(BATT_PIN);  // factory eFuse-calibrated mV
        delay(2);
    }
    float volts = (mv / 16.0f) * BATT_DIV * BATT_TRIM / 1000.0f;
    Serial.printf("Battery: %.2f V\n", volts);
    return volts;
}

// ── WiFi ─────────────────────────────────────────────────────────
bool connectWiFi() {
    Serial.printf("Connecting to WiFi '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("WiFi connection failed.");
    return false;
}

// ── MQTT publish ─────────────────────────────────────────────────
bool publishMQTT(int moisturePct, const char* status, float batt,
                 bool battLow, bool alert) {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);

    unsigned long start = millis();
    while (!mqtt.connected() && millis() - start < 5000) {
        if (mqtt.connect(MQTT_CLIENT_ID)) break;
        delay(250);
    }
    if (!mqtt.connected()) {
        Serial.println("MQTT connection failed.");
        return false;
    }

    char payload[192];
    snprintf(payload, sizeof(payload),
        "{\"moisture\":%d,\"status\":\"%s\",\"batt\":%.2f,\"batt_low\":%s,"
        "\"alert\":%s,\"wake\":%d,\"brownouts\":%u}",
        moisturePct, status, batt, battLow ? "true" : "false",
        alert ? "true" : "false", wakeCount, g_brownouts);

    char topic[64];
    snprintf(topic, sizeof(topic), "planter/%s/%s",
        PLANT_ID, alert ? "alert" : "moisture");
    bool ok = mqtt.publish(topic, payload);
    Serial.printf("MQTT → %s: %s [%s]\n", topic, payload, ok ? "ok" : "fail");
    mqtt.disconnect();
    return ok;
}

// ── Drawing helpers ──────────────────────────────────────────────
void drawDrop(int cx, int cy, int h, bool filled) {
    int r = h / 3;
    int bodyY = cy + h / 6;
    int tipY  = cy - h / 3;
    if (filled) {
        display.fillCircle(cx, bodyY, r, GxEPD_BLACK);
        display.fillTriangle(cx - r, bodyY, cx + r, bodyY, cx, tipY, GxEPD_BLACK);
    } else {
        display.drawCircle(cx, bodyY, r, GxEPD_BLACK);
        display.drawTriangle(cx - r, bodyY, cx + r, bodyY, cx, tipY, GxEPD_BLACK);
    }
}

// The pot and foliage take the moisture colour: a red plant reads as "this
// plant is in trouble" from the other end of the allotment, well before the
// numbers are legible.
void drawPlant(int cx, int cy, uint16_t colour) {
    display.fillRoundRect(cx - 18, cy + 8, 36, 6, 2, colour);
    display.fillRect(cx - 15, cy + 14, 30, 14, colour);
    display.fillRect(cx - 12, cy + 28, 24, 6, colour);
    display.fillRect(cx - 1, cy - 28, 3, 38, colour);
    display.fillCircle(cx - 10, cy - 18, 7, colour);
    display.fillCircle(cx - 16, cy - 22, 5, colour);
    display.fillCircle(cx + 10, cy - 10, 7, colour);
    display.fillCircle(cx + 16, cy - 14, 5, colour);
    display.fillCircle(cx, cy - 32, 6, colour);
    display.fillCircle(cx + 2, cy - 38, 4, colour);
}

// Frame stays black so the bar's extent is always readable; only the fill
// carries the alert colour. Ticks mark the two alert thresholds.
void drawBar(int x, int y, int w, int h, int pct, uint16_t fillColour) {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    int fill = (w - 4) * pct / 100;
    display.fillRect(x + 2, y + 2, fill, h - 4, fillColour);

    const int thresholds[] = { ALERT_DRY, ALERT_WET };
    for (int t : thresholds) {
        display.drawFastVLine(x + 2 + (w - 4) * t / 100, y - 4, 3, GxEPD_BLACK);
    }
}

// 3 px border, drawn only when something needs a human. On a B/W panel
// EPD_ACCENT is black, so this still works — it just isn't as loud.
void drawAlertFrame() {
    display.fillRect(0,   0,   200,   3, EPD_ACCENT);
    display.fillRect(0,   197, 200,   3, EPD_ACCENT);
    display.fillRect(0,   0,     3, 200, EPD_ACCENT);
    display.fillRect(197, 0,     3, 200, EPD_ACCENT);
}

// ── Status string ────────────────────────────────────────────────
const char* getStatus(int pct) {
    if      (pct < 20) return "Very Dry!";
    else if (pct < 40) return "Needs Water";
    else if (pct < 70) return "Good";
    else if (pct < 90) return "Moist";
    else               return "Very Wet";
}

// ── Main display render ──────────────────────────────────────────
// The e-paper retains its image unpowered, so whatever we draw here stays on
// screen all through sleep — including the "Charge me!" notice when conserving.
//
// Colour policy (§8): BLACK carries the information, the accent means "a human
// needs to do something". On the fitted B/W panel EPD_ACCENT is black, so the
// layout renders in one ink and simply loses the emphasis — no state depends on
// colour alone. The split is kept so a tri-colour build still looks right: red
// pigment has lower contrast and coarser edges, so it goes on large, low-detail
// elements — the frame, the plant, the big digits — never on fine strokes.
//
// Conservation outranks moisture for the accent: a flat cell makes the moisture
// figure meaningless (§6), so the whole screen goes red and says "Charge me!"
// rather than colouring itself by a reading it has just told you not to trust.
void updateDisplay(int moisturePct, float batt, bool battValid,
                   bool battSuspect, bool isConserving) {
    const bool moistureAlert =
        (moisturePct < ALERT_DRY || moisturePct > ALERT_WET);
    const bool anyAlert = isConserving || battSuspect || moistureAlert;
    const uint16_t moistCol =
        (isConserving || moistureAlert) ? EPD_ACCENT : GxEPD_BLACK;
    const uint16_t battCol =
        (isConserving || battSuspect)   ? EPD_ACCENT : GxEPD_BLACK;

    display.setRotation(0);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (anyAlert) drawAlertFrame();

        // Battery voltage, small, top-left. "!" suspect, "--" if no sense pin.
        // Inset to x=8 to clear the 3 px alert frame.
        char bv[12];
        if (!battValid)        snprintf(bv, sizeof(bv), "BAT --");
        else                   snprintf(bv, sizeof(bv), "%.2fV%s", batt,
                                        battSuspect ? "!" : "");
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(battCol);
        display.setCursor(8, 16);
        display.print(bv);

        drawPlant(100, 50, moistCol);

        if (moisturePct >= 25) drawDrop(80, 92, 14, true);
        if (moisturePct >= 50) drawDrop(100, 97, 14, true);
        if (moisturePct >= 75) drawDrop(120, 92, 14, true);

        display.drawLine(10, 115, 190, 115, GxEPD_BLACK);

        display.setFont(&FreeMonoBold24pt7b);
        display.setTextColor(moistCol);
        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", moisturePct);
        int16_t bx, by; uint16_t bw, bh;
        display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((200 - bw) / 2, 152);
        display.print(buf);

        // When conserving, the moisture figure is stale/untrusted — the user's
        // one job is to charge it, so say exactly that in the status slot.
        const char* status = isConserving ? "Charge me!" : getStatus(moisturePct);
        display.setFont(&FreeSans12pt7b);
        display.setTextColor(moistCol);
        display.getTextBounds(status, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((200 - bw) / 2, 176);
        display.print(status);

        drawBar(20, 185, 160, 10, moisturePct, moistCol);
    } while (display.nextPage());
}

// ── Redraw decision (§8) ─────────────────────────────────────────
// See REDRAW_ONLY_ON_CHANGE. Returns true when the panel image would actually
// differ in a way worth 14 s of refresh.
bool needsRedraw(int pct, bool alert, bool suspect,
                 bool isConserving, bool heartbeat) {
    if (!REDRAW_ONLY_ON_CHANGE)          return true;
    if (lastDrawnPct < -100)             return true;   // cold boot / power cycle
    if (heartbeat)                       return true;   // daily clean cycle
    if (isConserving != lastDrawnConserving) return true;
    if (alert        != lastDrawnAlert)      return true;
    if (suspect      != lastDrawnSuspect)    return true;
    // While conserving, the screen says "Charge me!" and the moisture figure is
    // frozen behind it — no reading is worth spending the refresh on.
    if (isConserving)                    return false;
    if (abs(pct - lastDrawnPct) >= REDRAW_DELTA) return true;
    // Band change inside the delta still changes the words on screen.
    return strcmp(getStatus(pct), getStatus(lastDrawnPct)) != 0;
}

// ── Setup (runs on every wake) ───────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);

    // Reset cause FIRST: a brownout reset means the cell sagged under load. A
    // sagging cell recovers open-circuit voltage in seconds and would pass the
    // voltage check, brown out again under WiFi, and loop — so trust the reset,
    // not the reading, and latch conservation regardless of what VBAT says.
    esp_reset_reason_t rr = esp_reset_reason();
    bool brownoutReset = (rr == ESP_RST_BROWNOUT);

    // NVS diagnostics — the only post-mortem with no serial console in the field.
    prefs.begin("planterv2", false);
    g_brownouts = prefs.getUInt("brownouts", 0);
    g_minVbatMv = prefs.getUInt("minVbatMv", 9999);
    if (brownoutReset) {
        g_brownouts++;
        prefs.putUInt("brownouts", g_brownouts);
        conserving = true;               // brownout == low battery, no argument
    }

    Serial.printf("\n── Planter v2 wake #%d (reset=%d) ──\n", wakeCount, (int)rr);
    Serial.printf("Diagnostics: brownouts=%u, minVbat=%u mV\n",
                  g_brownouts, g_minVbatMv);

    // Release the sleep holds so we can reconfigure the rail-side pins.
    releaseHolds();

    // Power the peripheral rail (sensor + panel share 3V3_SW via Q1).
    enablePeripheralRail();

    // Battery first — the conservation decision gates everything below.
    analogReadResolution(12);
    float batt = readBatteryVolts();
    bool battValid   = (batt >= BATT_VALID);
    bool battSuspect = battValid && (batt <= BATT_SUSPECT);

    // Update the min-VBAT low-water mark (only write NVS when it actually drops).
    if (battValid) {
        uint32_t mv = (uint32_t)(batt * 1000.0f + 0.5f);
        if (mv < g_minVbatMv) {
            g_minVbatMv = mv;
            prefs.putUInt("minVbatMv", g_minVbatMv);
        }
    }

    // Conservation state machine (hysteresis; brownout already latched above).
    // A floating/unwired sense pin (!battValid) never *enters* conservation on
    // voltage — only a real brownout can — so the board is safe to run before
    // the divider is confirmed.
    if (battValid) {
        if (conserving) {
            if (batt >= BATT_RESUME) conserving = false;   // recovered
        } else {
            if (batt <= BATT_CONSERVE) conserving = true;  // sagged
        }
    }

    // Read moisture (rail is up).
    int moisture = readMoisturePercent();
    const char* status = getStatus(moisture);
    Serial.printf("Moisture: %d%% (%s)  Battery: %s\n", moisture, status,
                  battValid ? "" : "(no sense pin)");
    if (battValid)
        Serial.printf("  VBAT %.2f V%s%s\n", batt,
                      battSuspect ? " [suspect]" : "",
                      conserving  ? " [CONSERVING]" : "");

    // Radio policy (§7): connect only on a moisture alert or the daily
    // heartbeat, and NEVER because the battery is low. Conservation suppresses
    // the radio entirely — that is the whole point of §6.
    //  - Moisture alerts are suppressed while the reading is battery-suspect:
    //    LDO dropout biases the sensor false-dry, so a "very dry" alert there is
    //    untrustworthy. The next healthy heartbeat still carries batt_low.
    bool moistureAlert = battValid && !battSuspect &&
                         (moisture < ALERT_DRY || moisture > ALERT_WET);
    bool heartbeat     = (wakeCount % HEARTBEAT_INTERVAL == 0);
    bool doRadio       = !conserving && (moistureAlert || heartbeat);

    // The panel shows the moisture band whether or not the radio trusts it, so
    // the redraw key tracks what is drawn, not the (suppressed) radio alert.
    bool displayAlert = (moisture < ALERT_DRY || moisture > ALERT_WET);

    // Init SPI + display now that the panel has power, render, then hibernate —
    // but only if the image would actually change (§8).
    if (needsRedraw(moisture, displayAlert, battSuspect, conserving, heartbeat)) {
        Serial.println("Initialising SPI + display...");
        SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
        display.init(115200, true, 2, false);
        updateDisplay(moisture, batt, battValid, battSuspect, conserving);
        display.hibernate();
        Serial.println("Display updated.");

        lastDrawnPct        = moisture;
        lastDrawnAlert      = displayAlert;
        lastDrawnSuspect    = battSuspect;
        lastDrawnConserving = conserving;
    } else {
        // Nothing to draw: park the panel's signal lines now rather than leave
        // them floating against a powered panel until the rail is gated below.
        parkPanelLines();
        Serial.printf("Display unchanged (last drawn %d%%), skipping refresh.\n",
                      lastDrawnPct);
    }

    // Rail OFF and lines parked BEFORE the (optional) radio window, so the
    // sensor/panel draw nothing while WiFi is up.
    disablePeripheralRail();

    if (doRadio) {
        Serial.printf("MQTT send: %s\n", moistureAlert ? "ALERT" : "heartbeat");
        if (connectWiFi()) {
            publishMQTT(moisture, status, batt, battSuspect, moistureAlert);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        }
    } else {
        Serial.printf("No radio this wake%s.\n",
                      conserving ? " (conserving)" : "");
    }

    wakeCount++;
    prefs.end();

    if (DEBUG_MODE) {
        Serial.println("DEBUG MODE: staying awake. Reset to re-read.");
        return;
    }

    uint32_t mins = conserving ? SLEEP_CONSERVE_MINUTES : SLEEP_MINUTES;
    Serial.printf("Sleeping for %u minutes%s...\n",
                  mins, conserving ? " (conserving)" : "");
    Serial.flush();

    // Latch the parked rail-side lines through deep sleep. PERIPH_EN is left
    // unlatched on purpose: R1 pulls it HIGH (rail OFF) the moment it floats.
    // C6's per-pad holds persist directly; S3 additionally needs the legacy
    // global deep-sleep hold gate.
    #if !defined(PLANTER_XIAO_C6)
      gpio_deep_sleep_hold_en();
    #endif
    esp_sleep_enable_timer_wakeup((uint64_t)mins * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {
    if (DEBUG_MODE) {
        // Rail is off after setup()'s disablePeripheralRail(); bring it back for
        // a live read. Use this loop to capture MOISTURE_DRY/WET and BATT_TRIM
        // on a v2 board (see the calibration warning at the top).
        enablePeripheralRail();
        int moisture = readMoisturePercent();
        float batt = readBatteryVolts();
        Serial.printf("  -> %d%% (%s)  batt %.2f V\n",
            moisture, getStatus(moisture), batt);
        disablePeripheralRail();
        delay(3000);
    }
}
