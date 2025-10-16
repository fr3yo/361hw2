// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "schedlab.h"

char LICENSE[] SEC("license") = "GPL";

enum ev_type {
    EV_WAKE     = 1,
    EV_SWITCH   = 2,
    EV_EXEC     = 3,
    EV_EXIT     = 4,
    EV_WAITLONG = 6,
    EV_FORK     = 7,   // added for sched_process_fork
};

/* existing handlers stay unchanged … */

/* === new minimal fork tracepoint === */
SEC("tp_btf/sched_process_fork")
int BPF_PROG(on_fork_btf, struct task_struct *parent, struct task_struct *child)
{
    struct event *e;
    __u64 now = bpf_ktime_get_ns();
    __u32 ppid = BPF_CORE_READ(parent, pid);
    __u32 cpid = BPF_CORE_READ(child, pid);

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type  = EV_FORK;
    e->pid   = ppid;

    /* reuse switch payload to record both */
    e->u.sw.prev_pid = ppid;
    e->u.sw.next_pid = cpid;
    bpf_core_read_str(e->u.sw.prev_comm, sizeof(e->u.sw.prev_comm), &parent->comm);
    bpf_core_read_str(e->u.sw.next_comm, sizeof(e->u.sw.next_comm), &child->comm);

    bpf_ringbuf_submit(e, 0);
    return 0;
}
