#include <yaos/types.h>
#include <yaos/sched.h>
#include <yaos/list.h>
#include <yaos/percpu.h>
#include <errno.h>
#include <yaos/printk.h>
#include <yaos/vm.h>
#include <asm/cpu.h>
#include <yaos/cpupm.h>
#include <yaos/atomic.h>
#include <asm/pm64.h>
#include <yaos/init.h>
#include <yaos/assert.h>
#if 1
#define DEBUG_PRINT printk
#else
#define DEBUG_PRINT inline_printk

#endif
extern ret_t switch_to(ulong stack, ulong * poldstack, ulong arg);
extern ret_t switch_first(struct thread_struct *, void *, ulong, ulong *, ulong);	//switch stack
extern ulong __max_phy_mem_addr;
u64 get_pte_with_addr(u64 addr);

ret_t yield_thread(ulong arg);
static DEFINE_SPINLOCK(kthead_create_lock);
static LIST_HEAD(kthread_create_list);
DEFINE_PER_CPU(struct task_struct, idle_task);
static inline ret_t thread_switch_first(struct thread_struct *p,
	void *thread_main,
	struct thread_struct *old, ulong arg)
{
    __thread ulong stack_point;

    stack_point = p->stack_addr + p->stack_size - 8;
    ASSERT(stack_point);
    ASSERT(stack_point < __max_phy_mem_addr || get_pte_with_addr(stack_point));
    return switch_first(p, thread_main, stack_point, &old->rsp, arg);
}

static inline ret_t thread_switch_to(struct thread_struct *p,
                                     struct thread_struct *old, ulong arg)
{
    if (!p->rsp)
        printk("%s,%lx,%lx,%lx\n", p->name, p, p->flag, p->state);
    ASSERT(p->rsp);
    ASSERT(p->rsp < __max_phy_mem_addr || get_pte_with_addr(p->rsp));
    return switch_to(p->rsp, &old->rsp, arg);
}

int create_thread_oncpu(struct thread_struct *pthread, int oncpu)
{
    if (likely(!pthread->stack_addr)) {
        ulong stack_addr = (ulong) alloc_vm_stack(pthread->stack_size);

        if (unlikely(!stack_addr)) {
            DEBUG_PRINT("no memory alloc stack:%lx\n", pthread->stack_size);
            return ENOMEM;
        }
        pthread->stack_addr = stack_addr;

    }
    spin_lock(&kthead_create_lock);
    list_add(&pthread->threads, &kthread_create_list);
    spin_unlock(&kthead_create_lock);
    spin_lock(&pthread->task->tasklock);
    list_add(&pthread->children, &pthread->task->threads);
    spin_unlock(&pthread->task->tasklock);
    pthread->state = THREAD_READY;
    pthread->on_cpu = oncpu;
    return 0;
}

atomic_t idlenr = ATOMIC_INIT(0);
static int idle_main(ulong arg)
{
    atomic_inc(&idlenr);

    for (;;)
        sti_hlt();
    return 0;
}

void goto_idle(void)
{
    idle_main(0);
}

static void thread_main(struct thread_struct *p, ulong arg)
{
    p->state = THREAD_RUN;
    DEBUG_PRINT("thread_main:%lx,%lx,%s\n", p, arg, p->name);
    while (p->flag & THREAD_SUSPEND)
        yield_thread(arg);
    p->main(arg);
}

ret_t exit_thread(ulong arg)
{
    struct thread_struct *p;
    cpu_p pcpu = get_current_cpu();
    struct thread_struct *pold = pcpu->current_thread;

    p = pold->parent;
    pold->state = THREAD_DONE;
    DEBUG_PRINT("thread exist:%lx,status:%d,%lx,%lx\n", arg, pold->state, pold,
                p);
    print_regs();
    if (unlikely(!p)) {
        panic("no parent thread running\n");
    }
    else {
        if (p->state == THREAD_READY) {

            p->state = THREAD_RUN;
            return thread_switch_first(p, thread_main, pold, arg);
            //return switch_first(p, thread_main, stack_point, &pold->rsp, arg);                                        //no return

        }
        else {
            p->state = THREAD_RUN;
            return thread_switch_to(p, pold, arg);
        }
    }
    DEBUG_PRINT("exec exist thread\n");
    for (;;)
        sti_hlt();
}

void wake_up_thread(pthread p)
{
    pthread pold;
    cpu_p pcpu = get_current_cpu();

    pold = pcpu->current_thread;
    printk("wake_up thread :%lx,%s,state:%lx,pold:%lx,flag:%lx\n", p, p->name,
           p->state, pold, p->flag);

    if (p == pold)
        return;
    if (p->state == THREAD_INIT)
        return;
    if (!(p->flag & THREAD_SUSPEND)) {
#ifdef DEBUG
        bool found = false;

        while (pold) {
            pold = pold->parent;
            if (pold == p) {
                found = true;
            }
        }
        ASSERT(!found);
#endif
        return;
    }
    if (pold->lvl < p->lvl) {
        ASSERT(p != pold->parent);
        p->parent = pold->parent;
        pold->parent = p;
        return;
    }
    p->flag &= ~THREAD_SUSPEND;
    pcpu->current_thread = p;
    p->parent = pold;
    if (p->state == THREAD_READY) {

        p->state = THREAD_RUN;
        thread_switch_first(p, thread_main, pold, 0);

    }
    else {
        //p->state = THREAD_RUN;
        thread_switch_to(p, pold, 0);
    }

}

void suspend_thread()
{
    pthread p;
    cpu_p pcpu = get_current_cpu();
    pthread pold = pcpu->current_thread;

    printk("suspend:%lx\n", pold);
    p = pold->parent;
    ASSERT(p != pold);
    if (unlikely(!p)) {
        panic("no parent thread running\n");
    }
    else {
        ASSERT(!(pold->flag & THREAD_SUSPEND));
        pold->flag |= THREAD_SUSPEND;
        pcpu->current_thread = p;
        if (p->state == THREAD_READY) {

            p->state = THREAD_RUN;
            thread_switch_first(p, thread_main, pold, 0);

        }
        else {
            p->state = THREAD_RUN;
            thread_switch_to(p, pold, 0);
        }

    }
}

ret_t resume_thread(pthread p, ulong arg)
{
    pthread pold;
    cpu_p pcpu = get_current_cpu();

    if (p->state == THREAD_DONE) {
        DEBUG_PRINT("resume done thread\n");
        ret_t t = { 0, -1 };
        return t;
    }
    pold = pcpu->current_thread;
    p->parent = pold;
    ASSERT(pold != p);
    pcpu->current_thread = p;
    if (p->state == THREAD_READY) {

        p->state = THREAD_RUN;
        return thread_switch_first(p, thread_main, pold, arg);

    }
    else {
        //p->state = THREAD_RUN;
        return thread_switch_to(p, pold, arg);
    }

}

ret_t yield_thread(ulong arg)
{
    pthread p;
    cpu_p pcpu = get_current_cpu();
    pthread pold = pcpu->current_thread;

    p = pold->parent;
    ASSERT(p != pold);
    if (unlikely(!p)) {
        panic("no parent thread running\n");
    }
    else {
        pcpu->current_thread = p;
        if (p->state == THREAD_READY) {

            p->state = THREAD_RUN;
            return thread_switch_first(p, thread_main, pold, arg);

        }
        else {
            p->state = THREAD_RUN;
            return thread_switch_to(p, pold, arg);
        }

    }
    ret_t t = { 0, -1 };
    return t;
}

int init_idle_thread()
{

    __this_cpu_rw_init();
    cpu_p cpu = this_cpu_ptr(&the_cpu);
    struct task_struct *task = this_cpu_ptr(&idle_task);

    task->mainthread.task = task;
    task->mainthread.stack_addr = (ulong) per_cpu_ptr(&init_stack, cpu->cpu);;
    task->mainthread.stack_size = sizeof(init_stack);
    task->mainthread.main = idle_main;
    task->mainthread.flag = THREAD_IDLE;
    task->mainthread.name = "idle";
    task->mainthread.parent = NULL;
    task->mainthread.real_parent = NULL;
    task->mainthread.lvl = THREAD_LVL_IDLE;
    task->parent = NULL;
    task->real_parent = NULL;

    int ret = create_thread_oncpu(&task->mainthread, cpu->cpu);

    if (ret)
        return ret;
    cpu->current_thread = &task->mainthread;
    task->mainthread.state = THREAD_RUN;
    return 0;
}

__init int static init_thread_call(bool isbp)
{
    init_idle_thread();
    return 0;
}

early_initcall(init_thread_call);
