#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>

// ── Pin definitions: ESP32-C3 SuperMini ──────────────────────────
// Moisture sensor
#define MOISTURE_PIN  1   // AOUT → GPIO 1 (ADC)

// E-ink display (WeAct 2.13", SSD1680)
// GPIO 9 is the BOOT button on SuperMini — may conflict with BUSY.
// Using -1 disables BUSY check (uses fixed delay instead).
// If this works, consider rewiring BUSY to a different GPIO (e.g. 3 or 10).
#define EPD_BUSY  -1  // was 9
#define EPD_RST   5
#define EPD_DC    0
#define EPD_CS    7
#define EPD_SCLK  4
#define EPD_MOSI  6

// ── Calibration ──────────────────────────────────────────────────
// Measure these with your sensor:
//   MOISTURE_DRY  = analogRead() with sensor in dry air
//   MOISTURE_WET  = analogRead() with sensor in water
// The defaults below are reasonable starting points.
#define MOISTURE_DRY  3825  // raw ADC in dry air
#define MOISTURE_WET  2358  // raw ADC in water (settled)

// ── Sleep ────────────────────────────────────────────────────────
#define SLEEP_MINUTES 30

// ── Display setup ────────────────────────────────────────────────
// WeAct 2.13" tri-color (black/white/red), 122x250, SSD1680
// If this doesn't work, try GxEPD2_213_Z19c or GxEPD2_213c
GxEPD2_3C<GxEPD2_213_Z98c, GxEPD2_213_Z98c::HEIGHT> display(
    GxEPD2_213_Z98c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ── Read moisture sensor ─────────────────────────────────────────
int readMoisturePercent() {
    // Average a few readings for stability
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
    // ── Pot ──
    // Rim
    display.fillRoundRect(cx - 18, cy + 8, 36, 6, 2, GxEPD_BLACK);
    // Body (slight taper faked with two rects)
    display.fillRect(cx - 15, cy + 14, 30, 14, GxEPD_BLACK);
    display.fillRect(cx - 12, cy + 28, 24, 6, GxEPD_BLACK);

    // ── Stem ──
    display.fillRect(cx - 1, cy - 28, 3, 38, GxEPD_BLACK);

    // ── Leaves ── (pairs of filled circles offset from stem)
    // Left leaf
    display.fillCircle(cx - 10, cy - 18, 7, GxEPD_BLACK);
    display.fillCircle(cx - 16, cy - 22, 5, GxEPD_BLACK);
    // Right leaf
    display.fillCircle(cx + 10, cy - 10, 7, GxEPD_BLACK);
    display.fillCircle(cx + 16, cy - 14, 5, GxEPD_BLACK);
    // Top leaf
    display.fillCircle(cx, cy - 32, 6, GxEPD_BLACK);
    display.fillCircle(cx + 2, cy - 38, 4, GxEPD_BLACK);
}

// Horizontal progress bar
void drawBar(int x, int y, int w, int h, int pct) {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    int fill = (w - 4) * pct / 100;
    display.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_BLACK);
}

// ── Main display render ──────────────────────────────────────────
void updateDisplay(int moisturePct) {
    // Simple test: white background, black text
    display.setRotation(1);  // Landscape: 250 wide × 122 tall
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);

        // ── Left side: plant icon ──
        drawPlant(50, 55);

        // Water drops under pot, more drops = more moisture
        if (moisturePct >= 25) drawDrop(35, 95, 14, true);
        if (moisturePct >= 50) drawDrop(52, 100, 14, true);
        if (moisturePct >= 75) drawDrop(69, 95, 14, true);

        // ── Vertical divider ──
        display.drawLine(108, 8, 108, 114, GxEPD_BLACK);

        // ── Right side: data ──
        // Percentage (red if dry, black otherwise)
        display.setFont(&FreeMonoBold24pt7b);
        uint16_t pctColor = (moisturePct < 20) ? GxEPD_RED : GxEPD_BLACK;
        display.setTextColor(pctColor);

        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", moisturePct);

        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
        int textX = 118 + (132 - bw) / 2;
        display.setCursor(textX, 45);
        display.print(buf);

        // Status label
        const char* status;
        uint16_t statusColor = GxEPD_BLACK;
        if      (moisturePct < 20) { status = "Very Dry!";    statusColor = GxEPD_RED; }
        else if (moisturePct < 40) { status = "Needs Water";  statusColor = GxEPD_RED; }
        else if (moisturePct < 70) { status = "Good"; }
        else if (moisturePct < 90) { status = "Moist"; }
        else                       { status = "Very Wet"; }

        display.setFont(&FreeSans12pt7b);
        display.setTextColor(statusColor);
        display.getTextBounds(status, 0, 0, &bx, &by, &bw, &bh);
        textX = 118 + (132 - bw) / 2;
        display.setCursor(textX, 72);
        display.print(status);

        // Moisture bar
        drawBar(124, 88, 120, 14, moisturePct);

    } while (display.nextPage());
}

// ── DEBUG MODE ───────────────────────────────────────────────────
// Set to false to enable deep sleep for production use
#define DEBUG_MODE false

// ── Setup (runs on every wake) ───────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);  // Extra delay so serial monitor can connect
    Serial.println("\n── Planter waking up ──");

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
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_MINUTES * 60 * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {
    if (DEBUG_MODE) {
        // Re-read and update every 60 seconds in debug mode
        delay(60000);
        int moisture = readMoisturePercent();
        Serial.printf("Moisture: %d%%\n", moisture);
        display.init(115200, true, 2, false);
        updateDisplay(moisture);
        display.hibernate();
    }
}
