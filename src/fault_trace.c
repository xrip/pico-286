#include "fault_trace.h"

#include <pico.h>
#include <hardware/structs/m33.h>
#include <hardware/irq.h>
#include <hardware/sync.h>

#if !PICO_RP2350 || !defined(__arm__)
#error "Fault trace requires the RP2350 Arm build"
#endif

volatile fault_trace_record_t fault_trace[2];
// Assembly names this allocation directly. Keep the symbol through LTO.
uint32_t fault_trace_stacks[2][64]
        __attribute__((used, externally_visible, aligned(8)));
_Static_assert(sizeof(fault_trace_stacks[0]) == 256, "Assembly stack stride");

void __attribute__((used, externally_visible, noreturn))
        __no_inline_not_in_flash_func(fault_trace_capture)(
                uintptr_t frame_sp, uint32_t exc_return,
                uint32_t original_msplim, uint32_t primask) {
    const unsigned core = get_core_num();
    volatile fault_trace_record_t *record = &fault_trace[core];
    record->core_seen = 0;
    record->sp = frame_sp;
    record->exc_return = exc_return;
    record->original_msplim = original_msplim;
    record->primask = primask;
    uint32_t basepri, faultmask;
    __asm volatile("mrs %0, basepri" : "=r"(basepri));
    __asm volatile("mrs %0, faultmask" : "=r"(faultmask));
    record->basepri = basepri;
    record->faultmask = faultmask;
    const uint32_t cfsr = m33_hw->cfsr;
    record->cfsr = cfsr;
    record->hfsr = m33_hw->hfsr;
    record->bfar = m33_hw->bfar;
    record->mmfar = m33_hw->mmfar;
    record->sfsr = m33_hw->sfsr;
    record->sfar = m33_hw->sfar;
    record->icsr = m33_hw->icsr;
    record->vtor = m33_hw->vtor;
    record->dma1_vector = 0;
    const unsigned dma_vector = VTABLE_FIRST_IRQ + DMA_IRQ_1;
    if (record->vtor >= 0x20000000u &&
        record->vtor <= 0x20082000u - (dma_vector + 1u) * sizeof(uint32_t))
        record->dma1_vector = ((const volatile uint32_t *)record->vtor)[dma_vector];
    record->pc = 0;
    record->lr = 0;
    record->xpsr = 0;
    record->frame_valid = 0;

    // Decode complete, default secure frames. FP data follows the core words.
    const unsigned fp_words = (exc_return & 0x10u) ? 0u : 18u;
    const unsigned frame_bytes = (fp_words + 8u) * sizeof(uint32_t);
    const uint32_t memmanage_stacking_errors = (1u << 3) | (1u << 4) | (1u << 5);
    const uint32_t stacking_errors = M33_CFSR_UFSR_STKOF_BITS |
            M33_CFSR_BFSR_STKERR_BITS | M33_CFSR_BFSR_UNSTKERR_BITS |
            M33_CFSR_BFSR_LSPERR_BITS | memmanage_stacking_errors;
    if ((exc_return & 0x60u) == 0x60u && !(cfsr & stacking_errors) &&
        (frame_sp & 3u) == 0 && frame_sp >= 0x20000000u &&
        frame_sp <= 0x20082000u - frame_bytes) {
        const volatile uint32_t *frame = (const volatile uint32_t *)frame_sp;
        record->lr = frame[5];
        record->pc = frame[6];
        record->xpsr = frame[7];
        record->frame_valid = 1;
    }
    __dmb();
    record->core_seen = core + 1u;
    for (;;) __asm volatile("wfi");
}

void __attribute__((naked, noreturn)) __not_in_flash_func(fault_trace_handler)(void) {
    __asm volatile(
        "mov r1, lr\n"
        "tst r1, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mrs r2, msplim\n"
        "mrs r3, primask\n"
        "cpsid i\n"
        "movs r4, #0\n"
        "msr msplim, r4\n"
        "ldr r5, =0xd0000000\n" // SIO CPUID is at offset zero.
        "ldr r5, [r5]\n"
        "lsls r5, r5, #8\n"
        "ldr r4, =fault_trace_stacks\n"
        "adds r4, r4, r5\n"
        "add.w r4, r4, #256\n"
        "msr msp, r4\n"
        "b fault_trace_capture\n"
    );
}
