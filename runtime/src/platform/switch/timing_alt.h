// mbedTLS timing_alt.h for Switch. Included as "timing_alt.h" when
// MBEDTLS_TIMING_ALT is defined (see runtime/CMakeLists.txt).
#ifndef MKW_SWITCH_TIMING_ALT_H
#define MKW_SWITCH_TIMING_ALT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mbedtls_timing_hr_time {
    uint64_t ticks;
};

typedef struct mbedtls_timing_delay_context {
    struct mbedtls_timing_hr_time timer;
    uint32_t int_ms;
    uint32_t fin_ms;
} mbedtls_timing_delay_context;

#ifdef __cplusplus
}
#endif

#endif
