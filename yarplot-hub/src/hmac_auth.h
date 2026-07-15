#pragma once
// ── Yarplot device-API request signing (HMAC-SHA256, mbedtls) ─────
// Mirrors the reference firmware/src/hmac_auth.h. The scheme:
//
//   body_hex     = sha256_hex(body_bytes)
//   canonical    = device_id \n timestamp \n nonce \n METHOD \n path \n body_hex
//   X-Device-Sig = hmac_sha256_hex(secret, canonical)
//
// All hex is lowercase. Fields are joined with '\n' (0x0A) — never
// "\r\n": one stray carriage return changes the digest and every
// request comes back 403. A GET has an empty body, whose sha256 is the
// well-known e3b0c4…b855, so GET and POST sign through one code path.
//
// The algorithm is verified against the spec's worked example below —
// see yp_selftest(). mbedtls, WiFi, HTTPClient etc. all ship with the
// arduino-esp32 core, so this needs no extra library.

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "mbedtls/md.h"

// Lowercase-hex encode n bytes into out (out must hold 2*n + 1 bytes).
static inline void yp_to_hex(const uint8_t *in, size_t n, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hx[(in[i] >> 4) & 0xF];
        out[2 * i + 1] = hx[in[i] & 0xF];
    }
    out[2 * n] = '\0';
}

// sha256 → 64-char lowercase hex + NUL (out must be >= 65 bytes).
static inline void yp_sha256_hex(const uint8_t *data, size_t len, char out[65]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t digest[32];
    mbedtls_md(info, data, len, digest);
    yp_to_hex(digest, 32, out);
}

// HMAC-SHA256 → 64-char lowercase hex + NUL (out must be >= 65 bytes).
static inline void yp_hmac_sha256_hex(const uint8_t *key, size_t keylen,
                                      const uint8_t *msg, size_t msglen,
                                      char out[65]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t mac[32];
    mbedtls_md_hmac(info, key, keylen, msg, msglen, mac);
    yp_to_hex(mac, 32, out);
}

// Build the canonical string and sign it. Returns false only if the
// canonical string would overflow the buffer (device_id / path too long).
static inline bool yp_sign(const char *device_id, const char *ts,
                           const char *nonce, const char *method,
                           const char *path, const uint8_t *body,
                           size_t body_len, const char *secret,
                           char out_sig[65]) {
    char body_hex[65];
    yp_sha256_hex(body, body_len, body_hex);

    char canonical[512];
    int n = snprintf(canonical, sizeof(canonical), "%s\n%s\n%s\n%s\n%s\n%s",
                     device_id, ts, nonce, method, path, body_hex);
    if (n <= 0 || n >= (int)sizeof(canonical)) return false;

    yp_hmac_sha256_hex((const uint8_t *)secret, strlen(secret),
                       (const uint8_t *)canonical, (size_t)n, out_sig);
    return true;
}

// Offline self-test against the spec's worked example (device-API v1,
// "Worked example"). Returns true iff this implementation reproduces
// the published body hash and signature. If it fails, the bug is here —
// fix it before touching the network.
static inline bool yp_selftest() {
    const char *body =
        "{\"device_id\":\"controller-1\",\"readings\":[],\"acks\":[]}";

    char body_hex[65];
    yp_sha256_hex((const uint8_t *)body, strlen(body), body_hex);
    if (strcmp(body_hex,
        "0cb2392f2f314818b76017ec0865bd1beac252b2829a8d9458e16f856d47b581"))
        return false;

    char sig[65];
    if (!yp_sign("controller-1", "1783936000", "a1b2c3d4e5f60718", "POST",
                 "/api/device/ingest", (const uint8_t *)body, strlen(body),
                 "test-secret", sig))
        return false;

    return strcmp(sig,
        "afc51cd3ace83d5c32288ac9bd2d826dcdb3f8ff259cc125d4baa70de5c894d7") == 0;
}
