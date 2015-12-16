#ifndef _ASM_SPINLOCK_H
#define _ASM_SPINLOCK_H
#include <asm/bitops.h>
#define barrier() __asm__ __volatile__("": : :"memory")
#define __SPIN_LOCK_UNLOCKED(x) 0
#define __SPIN_LOCK_LOCKED(x) 1
static inline void init_spinlock(spinlock_t * t)
{
    *t = 0;
}

static inline void spin_lock(spinlock_t * t)
{
    while (test_and_set_bit_lock(0, t)) {
    }
}

static inline void spin_unlock(spinlock_t * t)
{
    barrier();
    clear_bit(0, t);
}
#endif
