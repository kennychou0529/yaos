#ifndef __YAOS_SPINLOCK_H
#define __YAOS_SPINLOCK_H
typedef unsigned long spinlock_t;

#include <asm/spinlock.h>
#ifndef ARCH_SPINLOCK
void init_spinlock(spinlock_t *);
void spin_lock(spinlock_t *);
void spin_unlock(spinlock_t *);
#endif
#define DEFINE_SPINLOCK(x) spinlock_t x=__SPIN_LOCK_UNLOCKED(x)
#endif
