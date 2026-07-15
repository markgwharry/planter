# Yarplot allotment hub — headless ESP32-C3 soil-moisture node

A stripped-down, screen-free variant of the planter monitor that speaks the
[Yarplot device API v1](https://yarplot19.co.uk) instead of MQTT. It wakes on a
timer, reads its soil probes (and its own battery), POSTs one HMAC-signed batch
to `/api/device/ingest`, applies whatever config the server sends back, and
deep-sleeps. No e-ink, no display libraries — just the sensor, a cheap
microcontroller, and the radio.

It is built to the same signing scheme the Yarplot server verifies against its
own TypeScript and a reference ESP32/mbedtls implementation. The firmware
reproduces the spec's published worked example on every boot as a self-test
(see below), so you know the signing is correct before it ever touches the
network.

---

## Hardware

| Part | Notes |
|---|---|
| **ESP32-C3 SuperMini** | Cheapest ESP32 with WiFi + mbedtls; runs the reference signer unchanged. |
| Capacitive soil-moisture probe(s) | Same as the planter. One per bed you want to read. |
| 2× 220 kΩ resistors | Optional battery divider (see the planter's `NEW_HARDWARE.md`). |
| 1S LiPo / solar, or any 3.3 V supply | The plot is off-grid; battery or solar. |

### Where to buy the board (genuine, cheap)

- **[Tinkerverse — ESP32-C3 SuperMini](https://tinkerverse.co.uk/product/esp32-c3-supermini/)** — ~£6.99, UK stock, free Royal Mail, 2-day. Best balance of *legitimate* and *cheap* for a UK build.
- **[The board maker Seeed's XIAO ESP32-C3](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html)** — ~£5 direct from the manufacturer if you'd rather have a brand-name board with a known-good footprint. Same chip; the pin numbers in this sketch still apply, just check the silk.
- **AliExpress / official brand stores** — the SuperMini goes for ~£1.50–2.50 if you want rock-bottom, but quality varies. Buy from a high-rating store and expect to verify it enumerates as a genuine ESP32-C3 (`esptool.py chip_id`). Fine for spares; I'd start with the Tinkerverse one.

Avoid unbranded multipacks that don't name the vendor — that's where relabelled
or fake-silicon boards show up.

---

## Wiring (ESP32-C3 SuperMini)

ADC1 channels are `GPIO0`–`GPIO4`. Avoid `GPIO2/8/9` (strapping) and
`GPIO18/19` (USB). Defaults, all changeable at the top of `src/main.cpp`:

| Signal | GPIO | Pin role |
|---|---|---|
| Soil probe 1 AOUT | `GPIO0` | ADC1_CH0 → `sensor-hub-b1` |
| Soil probe 2 AOUT | `GPIO1` | ADC1_CH1 → `sensor-hub-b2` |
| Battery divider tap | `GPIO3` | ADC1_CH3 → `sensor-hub-batt` |
| Soil-probe VCC switch | `GPIO10` | HIGH powers the probes only while reading |
| Probe/​divider GND | `GND` | common ground |

One or two probes fit directly on ADC pins. For all six beds off a single hub,
add an analog multiplexer (e.g. CD4051) on one ADC pin and extend the sensor
table — but the simplest deployment is one small node per bed.

---

## Calibrate the probes (do not skip)

Datasheet numbers are worthless in soil, and an uncalibrated probe emits
plausible-looking values that the watering plan will divide real litres by.

1. In `src/main.cpp` set `#define CAL_MODE true`, flash, open the serial monitor.
2. Push the probe into **bone-dry** soil, read the raw ADC → set `RAW_DRY`.
3. Push it into **saturated** soil, read the raw ADC → set `RAW_WET`.
4. Set `CAL_MODE` back to `false` and reflash.

The firmware maps raw ADC linearly onto **%VWC** (the unit the sensor is
registered in). The spec's rule of thumb — ~8 %VWC bone dry, ~40 %VWC
saturated — is the default `VWC_DRY`/`VWC_SAT`; nudge them if you have a
reference. Readings outside a sane raw band are treated as a disconnected probe
and **omitted**, never faked.

---

## Register the device (Phil runs this once)

Nothing lands until the device and its sensors exist. This prints a device id
and a 64-character secret **once**:

```
node scripts/register-device.mjs --remote \
  --id hub-1 --name "Moisture hub" --poll 900 \
  --sensors "sensor-hub-b1:soil_moisture:pct_vwc:bed-b1,\
sensor-hub-b2:soil_moisture:pct_vwc:bed-b2,\
sensor-hub-batt:battery_voltage:V:hub-1"
```

The default sensor table in this sketch matches those three ids exactly, so the
command above and this firmware line up out of the box. If the hub **replaces**
a bed's existing controller probe, reuse that probe's id (`sensor-moist-b1`
etc.) and point it at this device instead; if it's **additional**, use new ids.

Get the secret over something private — it lives in Cloudflare KV and can't be
read back out of the app.

---

## Configure & flash

1. `cp src/secrets.example.h src/secrets.h` and fill in WiFi + the `DEVICE_ID`
   and `DEVICE_SECRET` from registration. `secrets.h` is git-ignored.
2. Flash with [PlatformIO](https://platformio.org/):
   ```
   pio run -e esp32c3 -t upload
   pio device monitor
   ```

On boot you should see:

```
== Yarplot hub wake (seq 0) ==
HMAC self-test: PASS
WiFi 192.168.x.x  RSSI -63
NTP ok, epoch 178...
Reading sensors...
  sensor-hub-b1     raw=2600  22.6 %VWC
  sensor-hub-batt   3.94 V
POST /api/device/ingest (2xx bytes)
HTTP 200
accepted=2 (stored, not sent)
Deep sleep 900 s
```

**`HMAC self-test: PASS`** means the signing reproduces the spec's worked
example (`controller-1` / `test-secret` → `afc51cd3…94d7`). If it ever says
`**FAIL**`, the bug is in `hmac_auth.h` — fix that before blaming the network.

---

## How it satisfies the bring-up checklist

| Checklist item | Where |
|---|---|
| Clock within 5 min (NTP) | `syncTime()` every time WiFi is up |
| Fresh random nonce per request | `makeNonce()` — 16 bytes of `esp_random()` |
| Canonical string joined with `\n` | `yp_sign()` in `hmac_auth.h` |
| Body hashed exactly as sent | body is hand-built once, hashed, then that same buffer is POSTed |
| Device + sensors registered; check `rejected` | `applyResponse()` prints any `rejected` ids |
| Probes calibrated dry/saturated | `CAL_MODE` + `RAW_DRY`/`RAW_WET` |
| Bad readings omitted, not faked | out-of-range raw / unwired battery are dropped |

Offline reads are timestamped and buffered in RTC memory, then flushed when WiFi
returns; the API dedupes on `(sensor_id, ts)`, so re-sending a batch is a no-op,
never a duplicate. "Sleeping for an hour is fine" — so is missing a few.

---

## A note on TLS (`setInsecure`)

`postIngest()` uses `client.setInsecure()` — it does not validate the server's
certificate. That's a deliberate, safe trade-off here: what authenticates the
request is the **HMAC**, and the secret is *never transmitted* (only the
signature is). A man-in-the-middle can't forge a request without the secret, and
can't learn it by listening. Replays are stopped by the single-use nonce. If
you'd still rather pin the chain, drop the ISRG Root X1 / Cloudflare root PEM
into `setCACert()` instead — the request signing doesn't change.

---

## WiFi backhaul, on an off-grid plot

The C3 is WiFi-only, and the spec says the plot has no mains WiFi. That's fine
because the device only ever makes **outbound** calls and tolerates being
offline for long stretches: provide backhaul however suits you — a phone
hotspot on a visit, a 4G MiFi at the shed, or bring the hub indoors — and the
buffered readings flush with their real measurement timestamps when a connection
appears. If you need genuinely unattended cellular, that's a different radio
(an LTE-M module) bolted onto the same protocol — the signing and payload code
here carry over unchanged.

---

## Extending to actuation (valves)

This build **reads only**, so `commands` always comes back empty and is ignored.
If a hub later drives a valve, the response may carry a command. Handle it in
`applyResponse()` and report back in `acks[]` on the *next* ingest — never open
a second connection. The four non-optional rules from the spec:

- Never run a command past `expires_at` (it's a stale intent from hours ago).
- Never exceed `max_runtime_s`, and enforce your own hard ceiling on top.
- `kill_switch: true` overrides everything — shut down, actuate nothing.
- **Fail closed**: whatever you drive must end up safe on a reset, brownout, or
  lost network, and report a truthful `actual_runtime_s` (the server reconciles
  it against tank level; a lie surfaces as a phantom leak).
