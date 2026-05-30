#include "uuidstr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

void uuidstr_fromstr(uuidstr_t *dest, const char *src) {
    memcpy(dest->data, src, UUIDSTR_LENGTH);
    dest->zero = 0;
}

void uuidstr_fromchars(uuidstr_t *dest, size_t len, const char *src) {
    if (len != UUIDSTR_LENGTH) {
        dest->data[0] = 0;
        return;
    }
    memcpy(dest, src, UUIDSTR_LENGTH);
    dest->zero = 0;
}

char *uuidstr_tostr(const uuidstr_t *src) {
    char *str = calloc(UUIDSTR_CAPACITY, sizeof(char));
    memcpy(str, src->data, UUIDSTR_LENGTH);
    return str;
}

bool uuidstr_t_equals_s(const uuidstr_t *a, const char *b) {
    return strncasecmp(a->data, b, UUIDSTR_LENGTH) == 0;
}

bool uuidstr_t_equals_t(const uuidstr_t *a, const uuidstr_t *b) {
    return strncasecmp(a->data, b->data, UUIDSTR_LENGTH) == 0;
}

bool uuidstr_is_empty(const uuidstr_t *uuid) {
    return uuid->data[0] == '0';
}

// Minimal v4 (random) UUID generator.
// Uses mbedTLS CTR_DRBG if available, falls back to time-based PRNG.
bool uuidstr_random(uuidstr_t *dest) {
    uint8_t buf[16];
    bool seeded = false;

    // Try mbedTLS CTR_DRBG first
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0) == 0) {
        mbedtls_ctr_drbg_random(&ctr_drbg, buf, sizeof(buf));
        seeded = true;
    }
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    // Fallback: time-based PRNG if mbedTLS entropy failed
    if (!seeded) {
        uint32_t t = (uint32_t)time(NULL);
        uint32_t seed = t ^ (uint32_t)(uintptr_t)&buf;
        for (int i = 0; i < 4; i++) {
            seed = seed * 1103515245 + 12345 + (i * 0x9e3779b1);
            uint32_t val = seed;
            memcpy(buf + i * 4, &val, 4);
        }
    }

    buf[6] = (buf[6] & 0x0F) | 0x40;  // version 4
    buf[8] = (buf[8] & 0x3F) | 0x80;  // variant 1

    snprintf(dest->data, UUIDSTR_LENGTH + 1,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             buf[0], buf[1], buf[2], buf[3],
             buf[4], buf[5], buf[6], buf[7],
             buf[8], buf[9], buf[10], buf[11],
             buf[12], buf[13], buf[14], buf[15]);
    dest->zero = 0;
    return true;
}
