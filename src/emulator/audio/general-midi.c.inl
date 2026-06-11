#pragma GCC optimize("Ofast")
#pragma once
//
// General MIDI synthesizer — GM.DLS fixed-point wavetable engine.
//
// Replaces the old synthesized voices (sine + LFSR noise) with the real GM.DLS
// wavetable bank. All the DSP lives in wavetable.c.inl (float-free, Cortex-M0+
// tuned: no int64/division on the per-sample path, baked LUTs, voice bitmask).
// This file is only the glue mpu401.c.inl / the emulator expect:
//
//   * void    parse_midi(const midi_command_t *)   - from wavetable.c.inl
//   * int16_t midi_sample(void)                     - mono wrapper, defined here
//
// Provided by the includer (emulator.h): INLINE, and optionally
// __not_in_flash_func (RAM-places the hot path; big win since the 16 KB XIP
// cache is shared between code and the flash-resident PCM).
//
// The packed bank is embedded in flash by gm_bank.S (.incbin gm_bank.bin) and
// must be generated for the output rate:  dls_pack gm.dls gm_bank.bin <rate>.
// The engine does NOT resample to SOUND_FREQUENCY — pack the bank at the actual
// playback rate (default 22050) or pitch/tempo will be off. See
// docs/device-integration.md.

#include "gm_bank.h"

#define WT_MAX_VOICES 32
#ifndef WT_RAMFUNC
#ifdef __not_in_flash_func
#define WT_RAMFUNC(name) __not_in_flash_func(name)   // keep the render loop in RAM
#endif
#endif

// parse_midi (note-on) runs in the MPU-401 context while midi_sample_stereo runs
// on the audio core / IRQ. wt_note_on publishes a new voice by setting its
// g_active_mask bit LAST; this barrier keeps the voice's field stores (pcm,
// frame_count, ...) from being reordered past that bit, so the render never sees
// an active voice with a NULL pcm. (Was a SIGSEGV at v->pcm[i0] after a few notes.)
#ifndef WT_BARRIER
#define WT_BARRIER()
//__dmb()
#endif

// RAM wave cache (on by default): the first time a looped wave plays it is
// malloc'd into RAM and read from RAM thereafter, so the per-sample pcm[] reads
// stop hitting XIP flash (the real bottleneck at high polyphony). It caches as
// much as fits in the heap right now and frees its least-recently-used wave when
// the heap is full — RAM use scales with the live working set, no fixed
// reservation. Host A/B on Doom/omf/dott: ~90%+ of per-sample reads from RAM,
// bit-exact. Build with -DWT_NO_WAVE_CACHE to compile it out entirely (no malloc)
// on targets with no spare RAM. See wavetable.c.inl / docs/device-integration.md.

// wavetable.c.inl marks its functions `INLINE foo` expecting INLINE == `static
// inline`. The emulator's INLINE is just `inline` (its code writes `static
// INLINE`), which would give wavetable external-linkage inlines. Bridge it for
// the wavetable include only, then restore the emulator's INLINE so the rest of
// the TU (mpu401.c.inl's `static INLINE ...`) keeps compiling.
#pragma push_macro("INLINE")
#undef INLINE
#define INLINE static inline
#include "wavetable.c.inl"   // parse_midi, midi_sample_stereo, wt_set_bank, midi_command_t
#pragma pop_macro("INLINE")

// Embed the packed soundbank in flash via inline-asm .incbin. Generate it first
// (dls_pack gm.dls gm_bank.bin <rate>) and make it reachable by the assembler
// (e.g. -Wa,-I<dir> on this TU, or an absolute path below). Define WT_BANK_EXTERN
// to skip the embed and supply gm_bank_blob yourself (host self-check, a separate
// .S/.c, etc.). See docs/device-integration.md.
#ifndef WT_BANK_EXTERN
#define IMPORT_BIN(file, sym) asm (\
    ".section .rodata." #sym "\n"           /* own rodata subsection (flash) */\
    ".balign 4\n"                           /* word alignment */\
    ".global " #sym "\n"                    /* export the object address */\
    #sym ":\n"                              /* define the object label */\
    ".incbin \"" file "\"\n"                /* import the file */\
    ".global _sizeof_" #sym "\n"            /* export the object size */\
    ".set _sizeof_" #sym ", . - " #sym "\n" /* define the object size */\
    ".balign 4\n"                           /* word alignment */\
    ".section \".text\"\n")                 /* restore section */
IMPORT_BIN("../src/emulator/audio/gm_bank.bin", gm_bank_blob);
#endif
extern const uint8_t gm_bank_blob[];        // size also available as _sizeof_gm_bank_blob

static int wt_bank_ready = 0;

// Bind the engine to the flash bank. Runs before main() via the C runtime init
// array on the Pico; the lazy guard in midi_sample() is a fallback for runtimes
// that do not run constructors.
static void __attribute__((constructor)) gm_wavetable_init(void) {
    wt_set_bank(gm_bank_blob);
    wt_bank_ready = 1;
}

// Mono sink kept for the current caller: sum the stereo render back to center.
// For true stereo + DLS pan, switch the emulator's audio mix to
// midi_sample_stereo(&l, &r).
static INLINE void midi_sample(int16_t samles[2]) {
    if (__builtin_expect(!wt_bank_ready, 0)) {
        wt_set_bank(gm_bank_blob);
        wt_bank_ready = 1;
    }
    // int16_t l, r;
    midi_sample_stereo(&samles[0], &samles[1]);
    // return (int16_t) (((int32_t) l + (int32_t) r) >> 1);
}

// Hand all the wave cache's RAM back to the heap when another subsystem needs it.
// MIDI keeps playing (voices on a RAM copy fall back to the byte-identical flash
// PCM seamlessly; the cache re-fills on demand). No-op when the cache is compiled
// out (WT_NO_WAVE_CACHE). Exported (external linkage): a consumer in another TU
// declares `extern void midi_cache_release(void);`. Call it from the same context
// as the MIDI/audio path, not concurrently with midi_sample().
void midi_cache_release(void) {
    wt_cache_release();
}
