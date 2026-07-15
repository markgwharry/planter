#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <time.h>
#include "hmac_auth.h"
#include "secrets.h"

// ══════════════════════════════════════════════════════════════════
//  Yarplot allotment hub — headless ESP32-C3 soil-moisture node
//  Board: ESP32-C3 SuperMini (generic esp32-c3-devkitm-1)
//
//  One round trip per wake: read the probes, POST a single signed
//  batch to /api/device/ingest, apply the server's config, deep-sleep.
//  No display. Readings measured while offline are buffered in RTC
//  memory and flushed when WiFi returns — the API dedupes on
//  (sensor_id, ts), so re-sending a batch is a no-op, never a dup.
// ══════════════════════════════════════════════════════════════════

// ── Firmware identity ────────────────────────────────────────────
#define FW_VERSION   "0.1.0"

// ── Pins: ESP32-C3 SuperMini ─────────────────────────────────────
// ADC1 channels are GPIO0–GPIO4. Avoid GPIO2/8/9 (strapping) and
// GPIO18/19 (USB). Change to match your wiring.
#define MOIST1_PIN    0     // GPIO0  / ADC1_CH0 → sensor-hub-b1 probe
#define MOIST2_PIN    1     // GPIO1  / ADC1_CH1 → sensor-hub-b2 probe
#define BATT_PIN      3     // GPIO3  / ADC1_CH3 → battery divider tap
#define SENSOR_PWR    10    // GPIO10 → switches soil-probe VCC (HIGH = on)

// ── Soil-probe calibration ───────────────────────────────────────
// CALIBRATE IN REAL SOIL. Datasheet figures are worthless in the
// ground. Enable CAL_MODE, read the raw ADC in bone-dry soil and again
// in saturated soil, then set these. The map is linear between the two
// points and is expressed in %VWC — the unit the sensor is registered
// in. Rule of thumb from the spec: ~8 %VWC bone dry, ~40 %VWC saturated.
#define RAW_DRY      3160   // raw ADC in bone-dry soil  → VWC_DRY
#define RAW_WET      1300   // raw ADC in saturated soil → VWC_SAT
#define VWC_DRY      8.0f
#define VWC_SAT      40.0f
// A probe reading outside this raw band is treated as disconnected/bad
// and OMITTED, not sent. The spec is explicit: if a reading is bad,
// omit it — never substitute something plausible, because the watering
// maths will trust the fabrication.
#define RAW_MIN_OK   200
#define RAW_MAX_OK   4000

// ── Battery divider (optional; same 2:1 mod as the planter) ──────
#define BATT_DIV     2.0f   // 220k/220k → 2:1
#define BATT_TRIM    1.0f   // single-point DMM trim = V_dmm / V_reported
#define BATT_VALID   2.50f  // below this the tap reads as unwired → omit

// ── Duty cycle ───────────────────────────────────────────────────
// Poll interval used before the server tells us its own via
// config.poll_interval_s. Matches the registration example (--poll 900).
#define DEFAULT_POLL_S   900
#define POLL_MIN_S       60
#define POLL_MAX_S       86400

// ── Network timeouts ─────────────────────────────────────────────
#define WIFI_TIMEOUT_MS  15000
#define NTP_TIMEOUT_MS   8000
#define HTTP_TIMEOUT_MS  10000

// ── Offline buffer (RTC memory survives deep sleep) ──────────────
#define BUF_CAP  24

// ── Calibration mode ─────────────────────────────────────────────
// true → stay awake, print each probe's raw ADC every 2 s so you can
// capture RAW_DRY / RAW_WET. Set back to false for normal operation.
#define CAL_MODE  false

// ── Sensor table ─────────────────────────────────────────────────
// Order and ids MUST match registration. This mirrors the spec's
// example device exactly:
//   sensor-hub-b1:soil_moisture:pct_vwc:bed-b1
//   sensor-hub-b2:soil_moisture:pct_vwc:bed-b2
//   sensor-hub-batt:battery_voltage:V:hub-1
enum SensorType { SOIL, BATTERY };
struct SensorDef {
    const char *id;
    SensorType  type;
    uint8_t     pin;
};
static const SensorDef SENSORS[] = {
    { "sensor-hub-b1",   SOIL,    MOIST1_PIN },
    { "sensor-hub-b2",   SOIL,    MOIST2_PIN },
    { "sensor-hub-batt", BATTERY, BATT_PIN   },
};
static constexpr int N_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

// ── RTC-persisted state (kept across deep sleep, lost on power cut) ─
struct BufReading { uint8_t idx; int64_t ts; float value; };
RTC_DATA_ATTR static BufReading rtcBuf[BUF_CAP];
RTC_DATA_ATTR static uint16_t   rtcBufCount  = 0;
RTC_DATA_ATTR static uint32_t   rtcDropped   = 0;   // dropped-oldest count
RTC_DATA_ATTR static uint32_t   rtcPollS     = 0;   // 0 = uninitialised
RTC_DATA_ATTR static uint32_t   rtcSeq       = 0;
RTC_DATA_ATTR static uint32_t   rtcBootWord  = 0;   // 0 = uninitialised
RTC_DATA_ATTR static int64_t    rtcBootEpoch = 0;   // epoch at first boot

// A freshly-measured reading, still in RAM this wake.
struct Sample { uint8_t idx; float value; bool valid; };

// ── Small helpers ────────────────────────────────────────────────
static bool timeValid() { return time(nullptr) > 1704067200; }  // > 2024-01-01

// 16 random bytes → 32 hex chars. esp_random() is a true RNG once the
// RF subsystem is up (it is, we sign with WiFi on). Fresh every request.
static void makeNonce(char out[33]) {
    uint8_t nb[16];
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = esp_random();
        memcpy(nb + i, &r, 4);
    }
    yp_to_hex(nb, 16, out);
}

// boot_id: stable across deep-sleep wakes, new on a power cycle.
static void makeBootId(char out[9]) {
    if (rtcBootWord == 0) rtcBootWord = esp_random();
    snprintf(out, 9, "%08x", rtcBootWord);
}

// Read one probe's raw ADC (assumes soil power is already applied).
static int readRaw(uint8_t pin) {
    analogReadResolution(12);
    long sum = 0;
    for (int i = 0; i < 16; i++) { sum += analogRead(pin); delay(8); }
    return (int)(sum / 16);
}

static float rawToVwc(int raw) {
    float v = VWC_DRY + (float)(raw - RAW_DRY) * (VWC_SAT - VWC_DRY) /
                        (float)(RAW_WET - RAW_DRY);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return v;
}

// Battery via the divider tap. No cap on the tap, so discard the first
// sample (lets the ADC S&H settle) and average the rest.
static float readBatteryV(uint8_t pin) {
    analogReadResolution(12);
    analogReadMilliVolts(pin);                    // discard first
    uint32_t mv = 0;
    for (int i = 0; i < 16; i++) { mv += analogReadMilliVolts(pin); delay(2); }
    return (mv / 16.0f) * BATT_DIV * BATT_TRIM / 1000.0f;
}

// Measure every sensor once. Bad/unwired readings come back valid=false
// and are omitted from the batch.
static void readAll(Sample out[]) {
    pinMode(SENSOR_PWR, OUTPUT);
    digitalWrite(SENSOR_PWR, HIGH);
    delay(100);                                   // let the probes settle
    for (int i = 0; i < N_SENSORS; i++) {
        out[i].idx = i;
        out[i].valid = false;
        if (SENSORS[i].type == SOIL) {
            int raw = readRaw(SENSORS[i].pin);
            if (raw >= RAW_MIN_OK && raw <= RAW_MAX_OK) {
                out[i].value = rawToVwc(raw);
                out[i].valid = true;
                Serial.printf("  %-16s raw=%4d  %.1f %%VWC\n",
                              SENSORS[i].id, raw, out[i].value);
            } else {
                Serial.printf("  %-16s raw=%4d  out of range -> omitted\n",
                              SENSORS[i].id, raw);
            }
        }
    }
    digitalWrite(SENSOR_PWR, LOW);

    // Battery divider is always-on; read it with the probes powered down.
    for (int i = 0; i < N_SENSORS; i++) {
        if (SENSORS[i].type == BATTERY) {
            float v = readBatteryV(SENSORS[i].pin);
            if (v >= BATT_VALID) {
                out[i].value = v; out[i].valid = true;
                Serial.printf("  %-16s %.3f V\n", SENSORS[i].id, v);
            } else {
                Serial.printf("  %-16s %.3f V < %.2f -> unwired, omitted\n",
                              SENSORS[i].id, v, BATT_VALID);
            }
        }
    }
}

// Bounded offline buffer. On overflow drop the oldest — and say so, so
// a silent cap never masquerades as "everything sent".
static void bufPush(uint8_t idx, int64_t ts, float value) {
    if (rtcBufCount >= BUF_CAP) {
        memmove(&rtcBuf[0], &rtcBuf[1], sizeof(BufReading) * (BUF_CAP - 1));
        rtcBufCount = BUF_CAP - 1;
        rtcDropped++;
        Serial.println("  offline buffer full -> dropped oldest reading");
    }
    rtcBuf[rtcBufCount++] = { idx, ts, value };
}

// Build the ingest body ONCE. We hash exactly these bytes and POST the
// same buffer — no re-serialising afterwards, which would change key
// order or spacing and break the signature. Hand-built for that reason.
// Returns the body length in bytes.
static size_t buildBody(char *buf, size_t cap, const Sample *now,
                        int64_t nowTs, float battTop) {
    char bootId[9]; makeBootId(bootId);
    int64_t uptime = (rtcBootEpoch > 0) ? (nowTs - rtcBootEpoch)
                                        : (int64_t)(millis() / 1000);
    size_t p = 0;
    p += snprintf(buf + p, cap - p,
        "{\"device_id\":\"%s\",\"fw\":\"%s\",\"boot_id\":\"%s\","
        "\"seq\":%u,\"uptime_s\":%lld",
        DEVICE_ID, FW_VERSION, bootId, (unsigned)rtcSeq, (long long)uptime);
    if (battTop >= 0)
        p += snprintf(buf + p, cap - p, ",\"battery_v\":%.2f", battTop);
    if (WiFi.isConnected())
        p += snprintf(buf + p, cap - p, ",\"rssi\":%d", (int)WiFi.RSSI());

    p += snprintf(buf + p, cap - p, ",\"readings\":[");
    bool first = true;
    // Buffered (offline) readings first — their real measurement time.
    for (uint16_t i = 0; i < rtcBufCount; i++) {
        p += snprintf(buf + p, cap - p,
            "%s{\"sensor_id\":\"%s\",\"ts\":%lld,\"value\":%.3f}",
            first ? "" : ",", SENSORS[rtcBuf[i].idx].id,
            (long long)rtcBuf[i].ts, rtcBuf[i].value);
        first = false;
    }
    // This wake's readings.
    for (int i = 0; i < N_SENSORS; i++) {
        if (!now[i].valid) continue;
        p += snprintf(buf + p, cap - p,
            "%s{\"sensor_id\":\"%s\",\"ts\":%lld,\"value\":%.3f}",
            first ? "" : ",", SENSORS[now[i].idx].id,
            (long long)nowTs, now[i].value);
        first = false;
    }
    p += snprintf(buf + p, cap - p, "],\"acks\":[]}");
    return p;
}

static bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS)
        delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi %s  RSSI %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.println("WiFi failed");
    return false;
}

// Keep the clock inside the spec's ±5 min window on every visit —
// clock skew is the single most common reason a new device gets 401.
static bool syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    unsigned long t0 = millis();
    while (!timeValid() && millis() - t0 < NTP_TIMEOUT_MS) delay(200);
    if (timeValid()) {
        Serial.printf("NTP ok, epoch %ld\n", (long)time(nullptr));
        return true;
    }
    Serial.println("NTP failed");
    return false;
}

// Sign and POST the ingest body. Returns the HTTP status (or a negative
// transport error) and fills respOut on a real HTTP response.
static int postIngest(const char *body, size_t len, String &respOut) {
    WiFiClientSecure client;
    // TLS is not what authenticates us — the HMAC is, and the secret is
    // never transmitted. So a skipped cert check can't forge or reveal
    // anything. Pin a root CA here instead if you prefer (see README).
    client.setInsecure();

    HTTPClient http;
    String url = String("https://") + YARPLOT_HOST + "/api/device/ingest";
    if (!http.begin(client, url)) return -1;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char ts[16];    snprintf(ts, sizeof(ts), "%ld", (long)time(nullptr));
    char nonce[33]; makeNonce(nonce);
    char sig[65];
    if (!yp_sign(DEVICE_ID, ts, nonce, "POST", "/api/device/ingest",
                 (const uint8_t *)body, len, DEVICE_SECRET, sig)) {
        http.end();
        return -2;
    }
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("X-Device-Id",    DEVICE_ID);
    http.addHeader("X-Device-Ts",    ts);
    http.addHeader("X-Device-Nonce", nonce);
    http.addHeader("X-Device-Sig",   sig);

    int code = http.POST((uint8_t *)body, len);
    if (code > 0) respOut = http.getString();
    http.end();
    return code;
}

// Read accepted / rejected / config / commands out of the response.
static void applyResponse(const String &body) {
    JsonDocument resp;
    if (deserializeJson(resp, body)) {
        Serial.println("response parse error");
        return;
    }

    int accepted = resp["accepted"] | -1;
    Serial.printf("accepted=%d (stored, not sent)\n", accepted);

    // rejected only appears when something was dropped. Non-empty means
    // an unregistered sensor_id — that data is gone. Check it every time.
    JsonArray rej = resp["rejected"];
    if (!rej.isNull() && rej.size() > 0) {
        Serial.print("REJECTED (unregistered sensor ids, data lost): ");
        for (JsonVariant v : rej) Serial.printf("%s ", v.as<const char *>());
        Serial.println();
    }

    long poll = resp["config"]["poll_interval_s"] | (long)0;
    if (poll >= POLL_MIN_S && poll <= POLL_MAX_S) {
        if ((uint32_t)poll != rtcPollS)
            Serial.printf("poll_interval_s -> %ld\n", poll);
        rtcPollS = (uint32_t)poll;
    }

    bool kill = resp["config"]["kill_switch"] | false;
    if (kill) Serial.println("kill_switch = true (human hit STOP)");

    // v1 reads sensors only, so the queue should always be empty. If a
    // command ever arrives we do NOT fake an ack — see the actuation
    // template in the README for the correct, fail-closed handling.
    JsonArray cmds = resp["commands"];
    if (!cmds.isNull() && cmds.size() > 0) {
        Serial.printf("%d command(s) received but this build actuates "
                      "nothing — ignoring (not acked). See README.\n",
                      (int)cmds.size());
    }
}

// CAL_MODE: raw ADC for each soil probe, forever, ~every 2 s.
static void calLoop() {
    Serial.println("CAL_MODE: capture raw ADC bone-dry and saturated.");
    pinMode(SENSOR_PWR, OUTPUT);
    for (;;) {
        digitalWrite(SENSOR_PWR, HIGH);
        delay(100);
        for (int i = 0; i < N_SENSORS; i++)
            if (SENSORS[i].type == SOIL)
                Serial.printf("%s raw=%d   ", SENSORS[i].id,
                              readRaw(SENSORS[i].pin));
        digitalWrite(SENSOR_PWR, LOW);
        for (int i = 0; i < N_SENSORS; i++)
            if (SENSORS[i].type == BATTERY)
                Serial.printf("%s=%.3fV", SENSORS[i].id,
                              readBatteryV(SENSORS[i].pin));
        Serial.println();
        delay(2000);
    }
}

// One wake's worth of work. rtcPollS (updated from config) sets the
// next sleep.
static void doWake() {
    bool online = connectWiFi();
    if (online) syncTime();

    // A reading needs a real measurement timestamp. Without a valid
    // clock (never synced, and offline now) we cannot timestamp, so we
    // skip rather than send something dated wrong.
    if (!timeValid()) {
        Serial.println("No valid time yet (never synced & offline) — "
                       "skipping this wake.");
        return;
    }

    int64_t nowTs = (int64_t)time(nullptr);
    if (rtcBootEpoch == 0) rtcBootEpoch = nowTs;

    Serial.println("Reading sensors...");
    Sample now[N_SENSORS];
    readAll(now);

    float battTop = -1;
    for (int i = 0; i < N_SENSORS; i++)
        if (SENSORS[i].type == BATTERY && now[i].valid) battTop = now[i].value;

    if (!online) {
        int n = 0;
        for (int i = 0; i < N_SENSORS; i++)
            if (now[i].valid) { bufPush(now[i].idx, nowTs, now[i].value); n++; }
        Serial.printf("Offline: buffered %d reading(s) (%u held).\n",
                      n, rtcBufCount);
        return;
    }

    static char body[4096];
    size_t len = buildBody(body, sizeof(body), now, nowTs, battTop);
    Serial.printf("POST /api/device/ingest (%u bytes)\n", (unsigned)len);

    String resp;
    int code = postIngest(body, len, resp);
    Serial.printf("HTTP %d\n", code);

    if (code == 200) {
        rtcSeq++;
        rtcBufCount = 0;                          // batch stored (dedupe-safe)
        if (rtcDropped) {
            Serial.printf("(note: %u reading(s) were dropped while offline)\n",
                          rtcDropped);
            rtcDropped = 0;
        }
        applyResponse(resp);
    } else {
        // Preserve this wake's readings and retry next time. Re-sending a
        // batch the server already stored is a no-op, so this is safe.
        Serial.println("Send failed; buffering this wake for retry.");
        for (int i = 0; i < N_SENSORS; i++)
            if (now[i].valid) bufPush(now[i].idx, nowTs, now[i].value);
        if (code == 401)
            Serial.println("  401 -> clock skew or malformed headers (check NTP).");
        if (code == 403)
            Serial.println("  403 -> bad signature, unknown device, or replay.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n== Yarplot hub wake (seq %u) ==\n", rtcSeq);

    // Prove the signing reproduces the spec's worked example before we
    // ever touch the network. If this says FAIL, nothing will authenticate
    // and the bug is in hmac_auth.h, not on the wire.
    Serial.printf("HMAC self-test: %s\n", yp_selftest() ? "PASS" : "**FAIL**");

    if (rtcPollS == 0) rtcPollS = DEFAULT_POLL_S;

    if (CAL_MODE) calLoop();                      // never returns

    doWake();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.printf("Deep sleep %u s\n", rtcPollS);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)rtcPollS * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}   // never runs: setup() ends in deep sleep
