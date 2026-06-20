#include "logi/Faults.h"
#include <stdio.h>

// Per-post fault accumulator. Plain uint32_t accessed through GCC __atomic
// builtins (no lock needed); flags are OR'd in from multiple tasks.
static uint32_t s_faults = 0;

typedef struct {
    uint32_t    flag;
    const char *code;
} fault_code_t;

// Order = bit order; drives the rendered string.
static const fault_code_t k_codes[] = {
    { FAULT_ADC,    "ADC" },
    { FAULT_AMB,    "AMB" },
    { FAULT_FUEL,   "FUEL" },
    { FAULT_GPS,    "GPS" },
    { FAULT_NTP,    "NTP" },
    { FAULT_WIFI,   "WIFI" },
    { FAULT_AWS,    "AWS" },
    { FAULT_LOWBAT, "LOWBAT" },
    { FAULT_CHG,    "CHG" },
    { FAULT_PWR,    "PWR" },
};
static const size_t k_num_codes = sizeof(k_codes) / sizeof(k_codes[0]);

void Faults_Set(fault_flag_t f)
{
    __atomic_or_fetch(&s_faults, (uint32_t)f, __ATOMIC_RELAXED);
}

uint32_t Faults_Get(void)
{
    return __atomic_load_n(&s_faults, __ATOMIC_RELAXED);
}

void Faults_Clear(void)
{
    __atomic_store_n(&s_faults, 0u, __ATOMIC_RELAXED);
}

void Faults_Render(char *err_out, size_t err_len, uint32_t *status_out)
{
    uint32_t f = Faults_Get();
    if (status_out) {
        *status_out = f;
    }
    if (!err_out || err_len == 0) {
        return;
    }
    err_out[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < k_num_codes; i++) {
        if (f & k_codes[i].flag) {
            const char *sep = (used > 0) ? "," : "";
            int n = snprintf(err_out + used, err_len - used, "%s%s", sep, k_codes[i].code);
            if (n < 0 || (size_t)n >= err_len - used) {
                break; // would truncate; stop cleanly
            }
            used += (size_t)n;
        }
    }
}
