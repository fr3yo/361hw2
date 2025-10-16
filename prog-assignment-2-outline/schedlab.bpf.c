// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "schedlab.h"

char LICENSE[] SEC("license") = "GPL";

/* === Existing Event Types === */
enum ev_type {
    EV_WAKE     = 1,
    EV_SWITCH   = 2,
    EV_EXEC     = 3,
    EV_EXIT     = 4,
    EV_WAITLONG = 6,
    EV_FORK     = 7,   /* new fork event */
};

/* === Pass Filter Helper === */
static __always_inline bool pass_filter(u32 pid)
{
    return pid > 0;
}

/* === Tracepoint: sched_wakeup === */
SEC("tp_btf/sched_wakeup")
int BPF_PROG(on_wake_btf, struct task_struct *p)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();
    __u32 pid = BPF_CORE_READ(p, pid);

    if (!pass_filter(pid))
        return 0;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type = EV_WAKE;
    e->pid = pid;
    bpf_core_read_str(e->comm, sizeof(e->comm), &p->comm);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* === Tracepoint: sched_switch === */
SEC("tp_btf/sched_switch")
int BPF_PROG(on_switch_btf, bool preempt,
             struct task_struct *prev, struct task_struct *next)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type = EV_SWITCH;
    e->pid = BPF_CORE_READ(next, pid);

    bpf_core_read_str(e->u.sw.prev_comm, sizeof(e->u.sw.prev_comm), &prev->comm);
    bpf_core_read_str(e->u.sw.next_comm, sizeof(e->u.sw.next_comm), &next->comm);
    e->u.sw.prev_pid = BPF_CORE_READ(prev, pid);
    e->u.sw.next_pid = BPF_CORE_READ(next, pid);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* === Tracepoint: sched_process_exec === */
SEC("tp_btf/sched_process_exec")
int BPF_PROG(on_exec_btf, struct task_struct *p, pid_t old_pid,
             struct linux_binprm *bprm)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();
    __u32 pid = BPF_CORE_READ(p, pid);

    if (!pass_filter(pid))
        return 0;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type = EV_EXEC;
    e->pid = pid;
    bpf_core_read_str(e->comm, sizeof(e->comm), &p->comm);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* === Tracepoint: sched_process_exit === */
SEC("tp_btf/sched_process_exit")
int BPF_PROG(on_exit_btf, struct task_struct *p)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();
    __u32 pid = BPF_CORE_READ(p, pid);

    if (!pass_filter(pid))
        return 0;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type = EV_EXIT;
    e->pid = pid;
    bpf_core_read_str(e->comm, sizeof(e->comm), &p->comm);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* === NEW Tracepoint: sched_process_fork === */
SEC("tp_btf/sched_process_fork")
int BPF_PROG(on_fork_btf, struct task_struct *parent, struct task_struct *child)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();
    __u32 ppid = BPF_CORE_READ(parent, pid);
    __u32 cpid = BPF_CORE_READ(child, pid);

    if (!pass_filter(ppid))
        return 0;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type  = EV_FORK;
    e->pid   = ppid;
    bpf_core_read_str(e->comm, sizeof(e->comm), &parent->comm);

    e->u.sw.prev_pid = ppid;
    e->u.sw.next_pid = cpid;
    bpf_core_read_str(e->u.sw.prev_comm, sizeof(e->u.sw.prev_comm), &parent->comm);
    bpf_core_read_str(e->u.sw.next_comm, sizeof(e->u.sw.next_comm), &child->comm);

    bpf_ringbuf_submit(e, 0);
    return 0;
}
