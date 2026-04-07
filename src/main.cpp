#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <GxEPD2_BW.h>
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
#define MOISTURE_DRY  3825  // raw ADC in dry air
#define MOISTURE_WET  2358  // raw ADC in water (settled)

// ── Sleep ────────────────────────────────────────────────────────
#define SLEEP_MINUTES 30

// ── Alert thresholds ────────────────────────────────────────────
#define ALERT_DRY     20   // below this → alert
#define ALERT_WET     90   // above this → alert
#define HEARTBEAT_INTERVAL 48  // wakes between heartbeats (48 × 30 min ≈ 24 h)

// ── MQTT ────────────────────────────────────────────────────────
#define MQTT_CLIENT_ID    "planter-" PLANT_ID

// ── RTC memory (survives deep sleep) ────────────────────────────
RTC_DATA_ATTR int wakeCount = 0;

// ── Display setup ────────────────────────────────────────────────
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
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
bool publishMQTT(int moisturePct, const char* status, bool alert) {
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

    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"moisture\":%d,\"status\":\"%s\",\"alert\":%s,\"wake\":%d}",
        moisturePct, status, alert ? "true" : "false", wakeCount);

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

void drawPlant(int cx, int cy) {
    display.fillRoundRect(cx - 18, cy + 8, 36, 6, 2, GxEPD_BLACK);
    display.fillRect(cx - 15, cy + 14, 30, 14, GxEPD_BLACK);
    display.fillRect(cx - 12, cy + 28, 24, 6, GxEPD_BLACK);

    display.fillRect(cx - 1, cy - 28, 3, 38, GxEPD_BLACK);

    display.fillCircle(cx - 10, cy - 18, 7, GxEPD_BLACK);
    display.fillCircle(cx - 16, cy - 22, 5, GxEPD_BLACK);
    display.fillCircle(cx + 10, cy - 10, 7, GxEPD_BLACK);
    display.fillCircle(cx + 16, cy - 14, 5, GxEPD_BLACK);
    display.fillCircle(cx, cy - 32, 6, GxEPD_BLACK);
    display.fillCircle(cx + 2, cy - 38, 4, GxEPD_BLACK);
}

void drawBar(int x, int y, int w, int h, int pct) {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    int fill = (w - 4) * pct / 100;
    display.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_BLACK);
}

// ── Main display render ──────────────────────────────────────────
void updateDisplay(int moisturePct) {
    display.setRotation(0);
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);

        drawPlant(100, 50);

        if (moisturePct >= 25) drawDrop(80, 92, 14, true);
        if (moisturePct >= 50) drawDrop(100, 97, 14, true);
        if (moisturePct >= 75) drawDrop(120, 92, 14, true);

        display.drawLine(10, 115, 190, 115, GxEPD_BLACK);

        display.setFont(&FreeMonoBold24pt7b);
        display.setTextColor(GxEPD_BLACK);

        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", moisturePct);

        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
        int textX = (200 - bw) / 2;
        display.setCursor(textX, 152);
        display.print(buf);

        const char* status;
        if      (moisturePct < 20) { status = "Very Dry!"; }
        else if (moisturePct < 40) { status = "Needs Water"; }
        else if (moisturePct < 70) { status = "Good"; }
        else if (moisturePct < 90) { status = "Moist"; }
        else                       { status = "Very Wet"; }

        display.setFont(&FreeSans12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.getTextBounds(status, 0, 0, &bx, &by, &bw, &bh);
        textX = (200 - bw) / 2;
        display.setCursor(textX, 176);
        display.print(status);

        drawBar(20, 185, 160, 10, moisturePct);

    } while (display.nextPage());
}

// ── Status string ────────────────────────────────────────────────
const char* getStatus(int pct) {
    if      (pct < 20) return "Very Dry!";
    else if (pct < 40) return "Needs Water";
    else if (pct < 70) return "Good";
    else if (pct < 90) return "Moist";
    else               return "Very Wet";
}

// ── DEBUG MODE ───────────────────────────────────────────────────
#define DEBUG_MODE true

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

    // Initialise SPI on custom pins
    Serial.println("Initialising SPI...");
    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

    // Initialise display
    Serial.println("Initialising display...");
    display.init(115200, true, 2, false);
    Serial.println("Display initialised.");

    // Read sensor
    analogReadResolution(12);
    int moisture = readMoisturePercent();
    const char* status = getStatus(moisture);
    Serial.printf("Moisture: %d%% (%s)\n", moisture, status);

    // Draw to e-ink
    Serial.println("Drawing to display...");
    updateDisplay(moisture);
    Serial.println("Display updated.");
    display.hibernate();

    // Decide whether to send MQTT
    bool alert = (moisture < ALERT_DRY || moisture > ALERT_WET);
    bool heartbeat = (wakeCount % HEARTBEAT_INTERVAL == 0);

    if (alert || heartbeat) {
        Serial.printf("MQTT send: %s\n", alert ? "ALERT" : "heartbeat");
        if (connectWiFi()) {
            publishMQTT(moisture, status, alert);
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
        delay(60000);
        int moisture = readMoisturePercent();
        Serial.printf("Moisture: %d%%\n", moisture);
        display.init(115200, true, 2, false);
        updateDisplay(moisture);
        display.hibernate();
    }
}
