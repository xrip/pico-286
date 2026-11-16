#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fault_trace_record {
    uint32_t core_seen; // 0 until complete; then core number + 1.
    uint32_t frame_valid;
    uint32_t sp;
    uint32_t pc;
    uint32_t lr;
    uint32_t xpsr;
    uint32_t exc_return;
    uint32_t original_msplim;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmfar;
    uint32_t sfsr;
    uint32_t sfar;
    uint32_t icsr;
    uint32_t vtor;
    uint32_t dma1_vector;
} fault_trace_record_t;

extern volatile fault_trace_record_t fault_trace[2];
void fault_trace_handler(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
