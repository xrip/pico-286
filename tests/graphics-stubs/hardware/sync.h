#pragma once
// Host stand-in: the tests run on one thread, so the spin lock does nothing.
typedef int spin_lock_t;
static inline int spin_lock_claim_unused(int required) { (void)required; return 0; }
static inline spin_lock_t *spin_lock_init(int number) { static spin_lock_t lock; (void)number; return &lock; }
static inline unsigned spin_lock_blocking(spin_lock_t *lock) { (void)lock; return 0; }
static inline void spin_unlock(spin_lock_t *lock, unsigned saved) { (void)lock; (void)saved; }
