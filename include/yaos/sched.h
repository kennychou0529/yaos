#ifndef _YAOS_SCHED_H
#define _YAOS_SCHED_H
#include <yaos/llist.h>
#include <yaos/spinlock.h>
#include <asm/current.h>
enum THREAD_FLAG_BITS {
    THREAD_PERCPU = 1,
    THREAD_SHOULD_STOP = 2,
    THREAD_IDLE = 4,
    THREAD_SUSPEND = 8,
};
enum {
    THREAD_INIT = 0,
    THREAD_READY = 1,
    THREAD_RUN = 2,
    THREAD_DONE = 3,
};
enum {
    THREAD_LVL_HIGHEST = 1,
    THREAD_LVL_TASKLET = 128,
    THREAD_LVL_WORKQUEUE = 512,
    THREAD_LVL_USER = 2048,
    THREAD_LVL_IDLE = 4096,
};
struct thread_struct;
typedef int (*thread_func) (ulong arg);
struct thread_struct {
    ulong stack_addr;
    ulong stack_size;
    ulong rsp;
    u32 lvl;
    volatile u32 flag;
    int (*main) (ulong arg);
    int on_cpu;
    volatile int state;
    struct list_head threads;
    struct thread_struct *real_parent;
    struct thread_struct *parent;
    struct task_struct *task;
    struct list_head children;
    struct list_head sibling;
    struct llist_node wake_entry;
    const char *name;
};
typedef struct thread_struct *pthread;
struct task_struct {
    struct list_head tasks;
    struct task_struct *real_parent;
    struct task_struct *parent;
    struct list_head threads;
    struct list_head children;
    struct list_head sibling;
    struct thread_struct mainthread;
    spinlock_t tasklock;
};
extern int init_sched(bool isbp);

extern void sched_yield(void);
extern void register_yield_callback(void (*pfunc) (void));
extern ret_t resume_thread(pthread p, ulong arg);
extern int create_thread_oncpu(struct thread_struct *pthread, int oncpu);
extern void wake_up_thread(pthread p);
extern void suspend_thread();
#endif
