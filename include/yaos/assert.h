/*
 * Copyright (C) 2015 Yiwu Saneee Network CO, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _YAOS_ASSERT_H_
#define _YAOS_ASSERT_H_ 1

#ifndef __
#ifdef DEBUG
#define BUG() do {} while (1)
#define ASSERT(x)                                                       \
do {                                                                    \
        if (!(x)) {                                                     \
                printf("assertion failed %s: %d: %s\n",      \
                       __FILE__, __LINE__, #x);                         \
                BUG();                                                  \
        }                                                               \
} while (0)
#else
#define ASSERT(x) do { } while (0)
#endif

#endif/*DEBUG*/

#endif
