#include <yaos/types.h>
#include <yaos/tasklet.h>
#include <yaos/barrier.h>
#include <yaos/percpu.h>
#include <yaos/irq.h>
#include <yaos/kernel.h>
#include <yaos/bug.h>
#include <yaos/init.h>
#include <yaos/sched.h>
#include <yaos/smp.h>
void __do_softirq(void);
extern u64 msec_count;
struct tasklet_head {
    struct tasklet_struct *head;
    struct tasklet_struct **tail;
};
static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec);
static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec);

DEFINE_PER_CPU(struct task_struct, tasklet_task);

static struct softirq_action softirq_vec[NR_SOFTIRQS]
 __cacheline_aligned_in_smp;

const char *const softirq_to_name[NR_SOFTIRQS] = {
    "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "BLOCK_IOPOLL",
    "TASKLET", "SCHED", "HRTIMER", "RCU"
};

static inline void invoke_softirq(void)
{
    __do_softirq();
}

void irq_enter(void)
{
}

void irq_exit(void)
{
    local_irq_disable();
    if (!in_tasklet() && local_softirq_pending())
        invoke_softirq();

}

void wakeup_taskletd(void)
{

    struct task_struct *tsk = this_cpu_ptr(&tasklet_task);

    //printk("tasklet_count:%d\n", tasklet_count);
    if (tsk->mainthread.flag & THREAD_SUSPEND) {
        wake_up_thread(&tsk->mainthread);
    }
}

static inline void __raise_softirq_irqoff(int nr)
{
    or_softirq_pending(1UL << nr);

}

static inline void raise_softirq_irqoff(unsigned int nr)
{
    __raise_softirq_irqoff(nr);

    /*
     * If we're in an interrupt or softirq, we're done
     * (this also catches softirq-disabled code). We will
     * actually run the softirq once we return from
     * the irq or softirq.
     *
     * Otherwise we wake up ksoftirqd to make sure we
     * schedule the softirq soon.
     */
    if (!in_tasklet())
        wakeup_taskletd();
}

void raise_softirq(unsigned int nr)
{
    unsigned long flags;

    flags = local_irq_save();
    raise_softirq_irqoff(nr);
    local_irq_restore(flags);

}

void open_softirq(int nr, void (*action) (struct softirq_action * p))
{
    softirq_vec[nr].action = action;

}

/* 2ms */
#define MAX_SOFTIRQ_TIME 2
#define MAX_SOFTIRQ_RESTART 10
void __do_softirq(void)
{
    unsigned long end = msec_count + MAX_SOFTIRQ_TIME;
    int max_restart = MAX_SOFTIRQ_RESTART;
    struct softirq_action *h;
    __u32 pending;
    int softirq_bit;

    /*
     * Mask out PF_MEMALLOC s current task context is borrowed for the
     * softirq. A softirq handled such as network RX might set PF_MEMALLOC
     * again if the socket is related to swap
     */

    pending = local_softirq_pending();
    disable_tasklet();
  restart:
    /* Reset the pending bitmask before enabling irqs */
    set_softirq_pending(0);

    local_irq_enable();

    h = softirq_vec;

    while ((softirq_bit = ffs(pending))) {

        h += softirq_bit - 1;

        h->action(h);
        h++;
        pending >>= softirq_bit;
    }

    local_irq_disable();
    pending = local_softirq_pending();
    if (pending) {
        if (msec_count < end && --max_restart)
            goto restart;

        wakeup_taskletd();
    }
    enable_tasklet();
}

void do_softirq(void)
{
    __u32 pending;
    unsigned long flags;

    if (in_tasklet())
        return;

    flags = local_irq_save();

    pending = local_softirq_pending();

    if (pending)
        do_softirq_own_stack();

    local_irq_restore(flags);
}

void __tasklet_schedule(struct tasklet_struct *t)
{
    unsigned long flags;

    flags = local_irq_save();
    t->next = NULL;
    *__this_cpu_read(tasklet_vec.tail) = t;
    __this_cpu_write(tasklet_vec.tail, &(t->next));
    raise_softirq_irqoff(TASKLET_SOFTIRQ);
    local_irq_restore(flags);
}

void __tasklet_hi_schedule(struct tasklet_struct *t)
{
    unsigned long flags;

    flags = local_irq_save();
    t->next = NULL;
    *__this_cpu_read(tasklet_hi_vec.tail) = t;
    __this_cpu_write(tasklet_hi_vec.tail, &(t->next));
    raise_softirq_irqoff(HI_SOFTIRQ);
    local_irq_restore(flags);
}

void __tasklet_hi_schedule_first(struct tasklet_struct *t)
{
    BUG_ON(!irqs_disabled());

    t->next = __this_cpu_read(tasklet_hi_vec.head);
    __this_cpu_write(tasklet_hi_vec.head, t);
    __raise_softirq_irqoff(HI_SOFTIRQ);
}

static void tasklet_action(struct softirq_action *a)
{
    struct tasklet_struct *list;

    local_irq_disable();
    list = __this_cpu_read(tasklet_vec.head);
    __this_cpu_write(tasklet_vec.head, NULL);
    __this_cpu_write(tasklet_vec.tail, this_cpu_ptr(&tasklet_vec.head));
    local_irq_enable();

    while (list) {
        struct tasklet_struct *t = list;

        list = list->next;

        if (tasklet_trylock(t)) {
            if (!atomic_read(&t->count)) {
                if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
                    BUG();
                t->func(t->data);
                tasklet_unlock(t);
                continue;
            }
            tasklet_unlock(t);
        }

        local_irq_disable();
        t->next = NULL;
        *__this_cpu_read(tasklet_vec.tail) = t;
        __this_cpu_write(tasklet_vec.tail, &(t->next));
        __raise_softirq_irqoff(TASKLET_SOFTIRQ);
        local_irq_enable();
    }
}

static void tasklet_hi_action(struct softirq_action *a)
{
    struct tasklet_struct *list;

    local_irq_disable();
    list = __this_cpu_read(tasklet_hi_vec.head);
    __this_cpu_write(tasklet_hi_vec.head, NULL);
    __this_cpu_write(tasklet_hi_vec.tail, this_cpu_ptr(&tasklet_hi_vec.head));
    local_irq_enable();

    while (list) {
        struct tasklet_struct *t = list;

        list = list->next;

        if (tasklet_trylock(t)) {
            if (!atomic_read(&t->count)) {
                if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
                    BUG();
                t->func(t->data);
                tasklet_unlock(t);
                continue;
            }
            tasklet_unlock(t);
        }
        local_irq_disable();
        t->next = NULL;
        *__this_cpu_read(tasklet_hi_vec.tail) = t;
        __this_cpu_write(tasklet_hi_vec.tail, &(t->next));
        __raise_softirq_irqoff(HI_SOFTIRQ);
        local_irq_enable();
    }
}

static int ksoftirqd_should_run(unsigned int cpu)
{
    return local_softirq_pending();
}

static int run_ksoftirqd(unsigned long cpu)
{
    local_irq_disable();
    if (local_softirq_pending()) {
        __do_softirq();
        local_irq_enable();
        //cond_resched_rcu_qs();
        return 0;
    }
    local_irq_enable();
    return 0;
}

void tasklet_init(struct tasklet_struct *t,
                  void (*func) (unsigned long), unsigned long data)
{
    t->next = NULL;
    t->state = 0;
    atomic_set(&t->count, 0);
    t->func = func;
    t->data = data;
}

static struct smp_percpu_thread tasklet_threads = {
    .task = &tasklet_task,
    .thread_should_run = ksoftirqd_should_run,
    .thread_fn = run_ksoftirqd,
    .thread_comm = "tasklet",
    .lvl = THREAD_LVL_TASKLET,
    .stack_size = TASKLET_STACK_SIZE,
};

int __init softirq_init(bool isbp)
{
    int cpu;
    struct task_struct *tsk = this_cpu_ptr(&tasklet_task);

    memset(tsk, 0, sizeof(*tsk));
    if (!isbp)
        return 0;
    for_each_possible_cpu(cpu) {
        per_cpu(tasklet_vec, cpu).tail = &per_cpu(tasklet_vec, cpu).head;
        per_cpu(tasklet_hi_vec, cpu).tail = &per_cpu(tasklet_hi_vec, cpu).head;
    }

    open_softirq(TASKLET_SOFTIRQ, tasklet_action);
    open_softirq(HI_SOFTIRQ, tasklet_hi_action);
    smpboot_register_percpu_thread(&tasklet_threads);
    return 0;
}

early_initcall(softirq_init);
