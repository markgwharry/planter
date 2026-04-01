#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>

// ── Pin definitions: Seeed XIAO ESP32-S3 ────────────────────────
// Moisture sensor
#define MOISTURE_PIN  1   // D0/A0 → GPIO 1 (ADC)

// E-ink display (Waveshare 1.54" B/W, SSD1681)
#define EPD_CS    2   // D1
#define EPD_DC    3   // D2
#define EPD_RST   4   // D3
#define EPD_BUSY  5   // D4
#define EPD_SCLK  7   // D8 — default HW SPI SCK
#define EPD_MOSI  9   // D10 — default HW SPI MOSI

// ── Calibration ──────────────────────────────────────────────────
// Measure these with your sensor:
//   MOISTURE_DRY  = analogRead() with sensor in dry air
//   MOISTURE_WET  = analogRead() with sensor in water
#define MOISTURE_DRY  3825  // raw ADC in dry air
#define MOISTURE_WET  2358  // raw ADC in water (settled)

// ── Sleep ────────────────────────────────────────────────────────
#define SLEEP_MINUTES 30

// ── Display setup ────────────────────────────────────────────────
// Waveshare 1.54" B/W, 200x200, SSD1681
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ── Read moisture sensor ─────────────────────────────────────────
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

// ── Drawing helpers ──────────────────────────────────────────────

// Simple water drop at (cx, cy) with given height
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

// Simple potted plant icon centred at (cx, cy)
void drawPlant(int cx, int cy) {
    // Pot rim
    display.fillRoundRect(cx - 18, cy + 8, 36, 6, 2, GxEPD_BLACK);
    // Pot body
    display.fillRect(cx - 15, cy + 14, 30, 14, GxEPD_BLACK);
    display.fillRect(cx - 12, cy + 28, 24, 6, GxEPD_BLACK);

    // Stem
    display.fillRect(cx - 1, cy - 28, 3, 38, GxEPD_BLACK);

    // Leaves
    display.fillCircle(cx - 10, cy - 18, 7, GxEPD_BLACK);
    display.fillCircle(cx - 16, cy - 22, 5, GxEPD_BLACK);
    display.fillCircle(cx + 10, cy - 10, 7, GxEPD_BLACK);
    display.fillCircle(cx + 16, cy - 14, 5, GxEPD_BLACK);
    display.fillCircle(cx, cy - 32, 6, GxEPD_BLACK);
    display.fillCircle(cx + 2, cy - 38, 4, GxEPD_BLACK);
}

// Horizontal progress bar
void drawBar(int x, int y, int w, int h, int pct) {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    int fill = (w - 4) * pct / 100;
    display.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_BLACK);
}

// ── Main display render (200x200 square) ─────────────────────────
void updateDisplay(int moisturePct) {
    display.setRotation(0);  // 200 x 200 square — no rotation needed
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);

        // ── Top section: plant icon ──
        drawPlant(100, 50);

        // Water drops under plant, more drops = more moisture
        if (moisturePct >= 25) drawDrop(80, 92, 14, true);
        if (moisturePct >= 50) drawDrop(100, 97, 14, true);
        if (moisturePct >= 75) drawDrop(120, 92, 14, true);

        // ── Divider ──
        display.drawLine(10, 115, 190, 115, GxEPD_BLACK);

        // ── Bottom section: data ──
        // Percentage
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

        // Status label
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

        // Moisture bar
        drawBar(20, 185, 160, 10, moisturePct);

    } while (display.nextPage());
}

// ── DEBUG MODE ───────────────────────────────────────────────────
#define DEBUG_MODE false

// ── Setup (runs on every wake) ───────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n── Planter waking up ──");

    // Release GPIO hold from deep sleep
    gpio_hold_dis((gpio_num_t)EPD_RST);
    gpio_hold_dis((gpio_num_t)EPD_DC);
    gpio_hold_dis((gpio_num_t)EPD_CS);
    gpio_hold_dis((gpio_num_t)EPD_SCLK);
    gpio_hold_dis((gpio_num_t)EPD_MOSI);
    gpio_deep_sleep_hold_dis();

    // Initialise SPI on custom pins (SCK, MISO, MOSI, SS)
    Serial.println("Initialising SPI...");
    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

    // Initialise display
    Serial.println("Initialising display...");
    display.init(115200, true, 2, false);
    Serial.println("Display initialised.");

    // Read sensor
    analogReadResolution(12);
    int moisture = readMoisturePercent();
    Serial.printf("Moisture: %d%%\n", moisture);

    // Draw to e-ink
    Serial.println("Drawing to display...");
    updateDisplay(moisture);
    Serial.println("Display updated.");
    display.hibernate();

    if (DEBUG_MODE) {
        Serial.println("DEBUG MODE: staying awake. Reset to re-read.");
    } else {
        Serial.printf("Sleeping for %d minutes...\n", SLEEP_MINUTES);
        Serial.flush();

        // Hold display pins stable during deep sleep
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
