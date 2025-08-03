#ifndef HOSTFS_H
#define HOSTFS_H

#if !PICO_ON_DEVICE

#include "emulator.h"
#include <stdbool.h>

// Initialize the host filesystem passthrough
void hostfs_init();

// Handle INT 21h (DOS services)
// Returns true if the interrupt was handled, false otherwise.
bool hostfs_int21h();

// Handle INT 2Fh (Multiplex)
// Returns true if the interrupt was handled, false otherwise.
bool hostfs_int2fh();

#endif // !PICO_ON_DEVICE

#endif // HOSTFS_H
