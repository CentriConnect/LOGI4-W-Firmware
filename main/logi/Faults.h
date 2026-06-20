#ifndef LOGI_FAULTS_H
#define LOGI_FAULTS_H

// Per-post fault accumulator for the telemetry `err` (string codes) + `deviceStatus`
// (int32 bitmask). Subsystems OR in a flag at their existing failure path; the
// telemetry context renders both fields before publish and clears after a good post.
// See docs/err-fault-spec.md.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAULT_ADC    = 1u << 0,  // ADS1015 read fail (battery/solar/supply/temp)
    FAULT_AMB    = 1u << 1,  // SHT4x read fail
    FAULT_FUEL   = 1u << 2,  // fuel read fail / 9705 status error
    FAULT_GPS    = 1u << 3,  // no GPS fix in the acquire window
    FAULT_NTP    = 1u << 4,  // NTP time sync fail
    FAULT_WIFI   = 1u << 5,  // Wi-Fi connect fail
    FAULT_AWS    = 1u << 6,  // AWS connect/publish fail
    FAULT_LOWBAT = 1u << 7,  // battery below threshold
    FAULT_CHG    = 1u << 8,  // charger error
    FAULT_PWR    = 1u << 9,  // power error
} fault_flag_t;

// OR a fault into the accumulator (atomic; safe from any task).
void Faults_Set(fault_flag_t f);

// Current accumulated bitmask.
uint32_t Faults_Get(void);

// Reset the accumulator (call after a successful final post).
void Faults_Clear(void);

// Render the accumulator: err_out = comma-separated codes ("ADC,GPS"; "" if none),
// *status_out = the raw bitmask. err_out is always NUL-terminated.
void Faults_Render(char *err_out, size_t err_len, uint32_t *status_out);

#ifdef __cplusplus
}
#endif

#endif // LOGI_FAULTS_H
