// mbedTLS platform hooks for Switch. Horizon has no Unix/Windows entropy or
// time source, so the mbedTLS build defines MBEDTLS_NO_PLATFORM_ENTROPY,
// MBEDTLS_ENTROPY_HARDWARE_ALT and MBEDTLS_PLATFORM_MS_TIME_ALT
// (see runtime/CMakeLists.txt). Both hooks are served by libnx here.
#ifdef MKW_PLATFORM_SWITCH
#include <switch.h>

#include <mbedtls/entropy.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/timing.h>

extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    if (len == 0) {
        *olen = 0;
        return 0;
    }
    randomGet(output, len);
    *olen = len;
    return 0;
}

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000;
}

extern "C" unsigned long mbedtls_timing_hardclock(void)
{
    return (unsigned long)armGetSystemTick();
}

extern "C" unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *val, int reset)
{
    uint64_t now = armTicksToNs(armGetSystemTick()) / 1000000;
    if (reset) {
        val->ticks = now;
        return 0;
    }
    return (unsigned long)(now - val->ticks);
}

extern "C" void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms)
{
    auto *ctx = static_cast<mbedtls_timing_delay_context *>(data);
    ctx->int_ms = int_ms;
    ctx->fin_ms = fin_ms;
    if (fin_ms != 0) {
        ctx->timer.ticks = armTicksToNs(armGetSystemTick()) / 1000000;
    }
}

extern "C" int mbedtls_timing_get_delay(void *data)
{
    auto *ctx = static_cast<const mbedtls_timing_delay_context *>(data);
    uint64_t now = armTicksToNs(armGetSystemTick()) / 1000000;
    uint64_t elapsed = now - ctx->timer.ticks;
    if (ctx->fin_ms == 0) {
        return -1;
    }
    if (elapsed >= ctx->fin_ms) {
        return 2;
    }
    if (elapsed >= ctx->int_ms) {
        return 1;
    }
    return 0;
}
#endif
