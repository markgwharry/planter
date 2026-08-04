#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ── Panel selection ──────────────────────────────────────────────
// Fitted panel is the B/W GDEH0154D67 (SSD1681) — the default. A tri-colour
// B/W/R module was fitted as a stopgap once after a panel was lost to ribbon
// damage; the build is back on B/W parts and we stock them, so the tri-colour
// options below are kept for compatibility, not because anything ships on one.
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

// ── Pin definitions: Seeed XIAO ESP32-S3 ────────────────────────
// Moisture sensor
#define MOISTURE_PIN  1   // D0/A0 → GPIO 1 (ADC)
#define SENSOR_PWR    6   // D5 → GPIO 6 — switches sensor VCC

// E-ink display (Waveshare 1.54" B/W, SSD1681)
#define EPD_CS    2   // D1
#define EPD_DC    3   // D2
#define EPD_RST   4   // D3
#define EPD_BUSY  5   // D4
#define EPD_SCLK  7   // D8 — default HW SPI SCK
#define EPD_MOSI  9   // D10 — HW SPI MOSI

// ── Calibration ──────────────────────────────────────────────────
#define MOISTURE_DRY  3160  // raw ADC bone-dry (asymptote, on 1S LiPo) → 0%
#define MOISTURE_WET  1300  // raw ADC submerged in water (settled)    → 100%

// ── Sleep ────────────────────────────────────────────────────────
#define SLEEP_MINUTES 30

// ── Battery monitor (1S LiPo via resistive divider) ─────────────
// Divider midpoint → an ADC1-capable pin (GPIO1–10). On THIS board the
// only free ADC pin is D9/GPIO8 — D4/GPIO5 is taken by EPD_BUSY.
// CONFIRM BATT_PIN against the actual wiring before trusting readings.
// No cap on the tap: readBatteryVolts() discards the first sample and
// averages to cope with the divider's source impedance.
#define BATT_PIN    8       // D9 / GPIO8, ADC1_CH7  ← set to your wired pin
#define BATT_DIV    2.0f    // nominal ratio for equal resistors
#define BATT_TRIM   1.0f    // single-point DMM trim: BATT_TRIM = V_dmm / V_reported
#define BATT_LOW    3.60f   // moisture reading suspect at/below this (LDO dropout)
#define BATT_CRIT   3.45f   // recharge now
#define BATT_VALID  2.50f   // below this the sense pin reads as unwired/floating:
                            // ignore it (no alert, show "--") so an unconfirmed
                            // divider can't spam false low-battery alerts.

// ── Alert thresholds ────────────────────────────────────────────
#define ALERT_DRY     20   // below this → alert
#define ALERT_WET     90   // above this → alert
#define HEARTBEAT_INTERVAL 48  // wakes between heartbeats (48 × 30 min ≈ 24 h)

// ── MQTT ────────────────────────────────────────────────────────
#define MQTT_CLIENT_ID    "planter-" PLANT_ID

// ── Redraw policy ────────────────────────────────────────────────
// A B/W refresh costs ~2.6 s of panel + MCU activity (~0.03 mAh). E-paper holds
// its image with the panel unpowered, so the cheapest refresh is the one we
// don't do: skip the redraw when nothing a human would notice has changed.
// Forced anyway on the daily heartbeat, so the panel still gets one clean full
// cycle every 24 h (keeps ghosting from building up) and a stuck reading can
// never masquerade as a fresh one for longer than a day.
//
// On this panel that saves ~1.2 mAh/day of a ~1.4 mAh/day budget — worth having,
// but not load-bearing. It was load-bearing on the tri-colour part, where a 14 s
// refresh every wake came to 7.7 mAh/day and dwarfed the rest of the design.
// Set false to go back to redrawing on every wake; on B/W that is a safe
// diagnostic step rather than a budget emergency.
#define REDRAW_ONLY_ON_CHANGE true
#define REDRAW_DELTA          3     // percentage points

// ── RTC memory (survives deep sleep, lost on power removal) ─────
RTC_DATA_ATTR int  wakeCount        = 0;
RTC_DATA_ATTR int  lastDrawnPct     = -1000;  // sentinel: nothing drawn yet
RTC_DATA_ATTR bool lastDrawnAlert   = false;
RTC_DATA_ATTR bool lastDrawnBattLow = false;

// ── Display setup ────────────────────────────────────────────────
// Full-height page buffer: 200x200 mono = 5 000 B per plane — one plane on the
// fitted B/W panel. GxEPD2_3C would keep two (black + red) = 10 kB, still
// trivial on the S3, so either way we render in a single page.
EPD_CLASS<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ── WiFi + MQTT clients ─────────────────────────────────────────
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ── Read moisture sensor ─────────────────────────────────────────
int readMoisturePercent() {
    pinMode(SENSOR_PWR, OUTPUT);
    digitalWrite(SENSOR_PWR, HIGH);
    delay(100);

    long sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogRead(MOISTURE_PIN);
        delay(10);
    }
    int raw = sum / 16;

    digitalWrite(SENSOR_PWR, LOW);
    Serial.printf("Moisture raw ADC: %d\n", raw);

    int pct = map(raw, MOISTURE_DRY, MOISTURE_WET, 0, 100);
    return constrain(pct, 0, 100);
}

// ── Read battery voltage ─────────────────────────────────────────
// Reads the divider tap on BATT_PIN and scales back to cell voltage.
// No cap on the tap, so discard the first sample (lets the ADC S&H
// settle against the divider impedance) and average the rest.
float readBatteryVolts() {
    analogReadResolution(12);
    analogReadMilliVolts(BATT_PIN);            // discard first
    uint32_t mv = 0;
    for (int i = 0; i < 16; i++) {
        mv += analogReadMilliVolts(BATT_PIN);  // factory-calibrated mV
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
bool publishMQTT(int moisturePct, const char* status, float batt, bool battLow, bool alert) {
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

    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"moisture\":%d,\"status\":\"%s\",\"batt\":%.2f,\"batt_low\":%s,\"alert\":%s,\"wake\":%d}",
        moisturePct, status, batt, battLow ? "true" : "false",
        alert ? "true" : "false", wakeCount);

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
// Colour policy: BLACK carries the information, the accent means "a human needs
// to do something". On the fitted B/W panel EPD_ACCENT is black, so the layout
// renders in one ink and simply loses the emphasis — no state depends on colour
// alone. The split is kept so a tri-colour build still looks right: red pigment
// has lower contrast and coarser edges, so it goes on large, low-detail
// elements — the frame, the plant, the big digits — never on fine strokes.
void updateDisplay(int moisturePct, float batt, bool battLow) {
    const bool moistureAlert  = (moisturePct < ALERT_DRY || moisturePct > ALERT_WET);
    const uint16_t moistCol   = moistureAlert ? EPD_ACCENT : GxEPD_BLACK;
    const uint16_t battCol    = battLow       ? EPD_ACCENT : GxEPD_BLACK;

    display.setRotation(0);
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);

        if (moistureAlert || battLow) drawAlertFrame();

        // Battery voltage, small, top-left. "!" when low, "--" if no sense pin.
        // Inset to x=8 to clear the 3 px alert frame.
        char bv[12];
        if (batt < BATT_VALID) snprintf(bv, sizeof(bv), "BAT --");
        else snprintf(bv, sizeof(bv), "%.2fV%s", batt, battLow ? "!" : "");
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

        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
        int textX = (200 - bw) / 2;
        display.setCursor(textX, 152);
        display.print(buf);

        const char* status = getStatus(moisturePct);

        display.setFont(&FreeSans12pt7b);
        display.setTextColor(moistCol);
        display.getTextBounds(status, 0, 0, &bx, &by, &bw, &bh);
        textX = (200 - bw) / 2;
        display.setCursor(textX, 176);
        display.print(status);

        drawBar(20, 185, 160, 10, moisturePct, moistCol);

    } while (display.nextPage());
}

// ── Redraw decision ──────────────────────────────────────────────
// See REDRAW_ONLY_ON_CHANGE. Returns true when the panel image would actually
// differ in a way worth 14 s of refresh.
bool needsRedraw(int pct, bool alert, bool battLow, bool heartbeat) {
    if (!REDRAW_ONLY_ON_CHANGE)   return true;
    if (lastDrawnPct < -100)      return true;   // cold boot / power cycle
    if (heartbeat)                return true;   // daily clean cycle
    if (alert   != lastDrawnAlert)   return true;
    if (battLow != lastDrawnBattLow) return true;
    if (abs(pct - lastDrawnPct) >= REDRAW_DELTA) return true;
    // Band change inside the delta still changes the words on screen.
    return strcmp(getStatus(pct), getStatus(lastDrawnPct)) != 0;
}

// ── DEBUG MODE ───────────────────────────────────────────────────
// true  → stays awake, prints raw ADC + % every 3 s for calibration.
//         Capture raw in dry air → MOISTURE_DRY, raw submerged → MOISTURE_WET.
// false → normal deep-sleep operation.
#define DEBUG_MODE false

// ── Setup (runs on every wake) ───────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("\n── Planter waking up (wake #%d) ──\n", wakeCount);

    // Release GPIO hold from deep sleep
    gpio_hold_dis((gpio_num_t)EPD_RST);
    gpio_hold_dis((gpio_num_t)EPD_DC);
    gpio_hold_dis((gpio_num_t)EPD_CS);
    gpio_hold_dis((gpio_num_t)EPD_SCLK);
    gpio_hold_dis((gpio_num_t)EPD_MOSI);
    gpio_deep_sleep_hold_dis();

    // Read sensors first: the redraw decision below depends on them, and the
    // panel costs less to leave asleep than to init and not use.
    analogReadResolution(12);
    int moisture = readMoisturePercent();
    const char* status = getStatus(moisture);
    float batt = readBatteryVolts();
    bool battValid = (batt >= BATT_VALID);
    bool battLow = battValid && (batt <= BATT_LOW);
    Serial.printf("Moisture: %d%% (%s)  Battery: %s",
        moisture, status, battValid ? "" : "(no sense pin)\n");
    if (battValid) Serial.printf("%.2f V%s\n", batt, battLow ? " (LOW)" : "");

    // Decide whether to send MQTT (low battery is itself an alert)
    bool alert = (moisture < ALERT_DRY || moisture > ALERT_WET || battLow);
    bool heartbeat = (wakeCount % HEARTBEAT_INTERVAL == 0);

    // Draw to e-ink, but only if the image would actually change. SPI and the
    // panel are brought up inside this branch for the same reason.
    if (needsRedraw(moisture, alert, battLow, heartbeat)) {
        Serial.println("Initialising SPI + display...");
        SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
        display.init(115200, true, 2, false);

        Serial.println("Drawing to display...");
        updateDisplay(moisture, batt, battLow);
        display.hibernate();
        Serial.println("Display updated.");

        lastDrawnPct     = moisture;
        lastDrawnAlert   = alert;
        lastDrawnBattLow = battLow;
    } else {
        Serial.printf("Display unchanged (last drawn %d%%), skipping refresh.\n",
            lastDrawnPct);
    }

    if (alert || heartbeat) {
        Serial.printf("MQTT send: %s\n", alert ? "ALERT" : "heartbeat");
        if (connectWiFi()) {
            publishMQTT(moisture, status, batt, battLow, alert);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        }
    } else {
        Serial.println("No MQTT needed this wake.");
    }

    wakeCount++;

    if (DEBUG_MODE) {
        Serial.println("DEBUG MODE: staying awake. Reset to re-read.");
    } else {
        Serial.printf("Sleeping for %d minutes...\n", SLEEP_MINUTES);
        Serial.flush();

        gpio_hold_en((gpio_num_t)EPD_RST);
        gpio_hold_en((gpio_num_t)EPD_DC);
        gpio_hold_en((gpio_num_t)EPD_CS);
        gpio_hold_en((gpio_num_t)EPD_SCLK);
        gpio_hold_en((gpio_num_t)EPD_MOSI);
        gpio_deep_sleep_hold_en();

        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_MINUTES * 60 * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {
    if (DEBUG_MODE) {
        // readMoisturePercent()/readBatteryVolts() already print their raw
        // values. No e-ink redraw here — saves the panel, keeps it snappy.
        // Use the battery line here to set BATT_TRIM against a DMM.
        int moisture = readMoisturePercent();
        float batt = readBatteryVolts();
        Serial.printf("  -> %d%% (%s)  batt %.2f V\n",
            moisture, getStatus(moisture), batt);
        delay(3000);
    }
}
