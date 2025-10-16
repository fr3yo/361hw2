// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

/* ---------------- Event model ---------------- */

enum ev_type {
    EV_WAKE     = 1,
    EV_SWITCH   = 2,
    EV_EXEC     = 3,
    EV_EXIT     = 4,
    EV_WAITLONG = 6,  /* wait latency >= threshold */
    EV_FORK     = 7,  /* new event for fork tracing */
};

struct ev_switch_payload {
    __u32 prev_pid, next_pid;
    char  prev_comm[16], next_comm[16];
    __u64 run_ns;         /* how long prev ran in this slice */
    __u64 wait_ns;        /* next’s wake->switch latency     */
    __s32 prev_cpu, next_cpu;
};

struct event {
    __u64 ts_ns;
    __u32 type;   /* ev_type */
    __u32 pid;    /* primary pid for convenience */
    char  comm[16];
    union {
        struct ev_switch_payload  sw;
    } u;
};

/* ---------------- Maps ---------------- */

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 512 * 1024);
} rb SEC(".maps");

/* pid -> last wake timestamp */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 131072);
    __type(key, __u32);
    __type(value, __u64);
} wake_ts SEC(".maps");

/* pid -> last time it began running (for run_ns on switch-out) */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 131072);
    __type(key, __u32);
    __type(value, __u64);
} oncpu_ts SEC(".maps");

/* Per-PID aggregates (for fairness, counts, etc.) */
struct agg {
    __u64 total_run_ns;
    __u64 total_wait_ns;
    __u64 switches;
    __u64 wakes;
    __u64 exec_ts_ns; /* first exec ts we saw for that pid */
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 131072);
    __type(key, __u32);
    __type(value, struct agg);
} agg_by_pid SEC(".maps");

/* Config knobs */
struct cfg {
    __u64 wait_alert_ns;     /* EV_WAITLONG threshold; 0=disabled */
    __u32 sample_filter_pid; /* 0=off; if set, only emit this pid's events */
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct cfg);
} cfg_map SEC(".maps");

/* ---------------- Helpers ---------------- */

static __always_inline int cfg_load(struct cfg *out)
{
    __u32 k = 0;
    struct cfg *c = bpf_map_lookup_elem(&cfg_map, &k);
    if (!c)
        return -1;
    __builtin_memcpy(out, c, sizeof(*out));
    return 0;
}

static __always_inline bool pass_filter(__u32 pid)
{
    struct cfg c;
    if (cfg_load(&c) == 0 && c.sample_filter_pid && c.sample_filter_pid != pid)
        return false;
    return true;
}

/* Ensure per-pid agg exists, return pointer for in-place updates. */
static __always_inline struct agg *agg_touch(__u32 pid)
{
    struct agg *a = bpf_map_lookup_elem(&agg_by_pid, &pid);
    if (!a) {
        struct agg zero = {};
        bpf_map_update_elem(&agg_by_pid, &pid, &zero, BPF_NOEXIST);
        a = bpf_map_lookup_elem(&agg_by_pid, &pid);
    }
    return a;
}

/* ---------------- tp_btf handlers ---------------- */

SEC("tp_btf/sched_wakeup")
int BPF_PROG(on_wakeup_btf, struct task_struct *p, int success)
{
    __u64 now = bpf_ktime_get_ns();
    __u32 pid = BPF_CORE_READ(p, pid);

    if (!pass_filter(pid))
        return 0;

    bpf_map_update_elem(&wake_ts, &pid, &now, BPF_ANY);
    struct agg *a = agg_touch(pid);
    if (a)
        a->wakes++;

    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = now;
    e->type  = EV_WAKE;
    e->pid   = pid;
    bpf_core_read_str(e->comm, sizeof(e->comm), &p->comm);
    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* ---- sched_switch ---- */
SEC("tp_btf/sched_switch")
int BPF_PROG(on_switch_btf, bool preempt, struct task_struct *prev,
             struct task_struct *next, unsigned int prev_state)
{
    __u64 now = bpf_ktime_get_ns();
    __u32 prev_pid = BPF_CORE_READ(prev, pid);
    __u32 next_pid = BPF_CORE_READ(next, pid);
    __u64 *on_ptr, *w_ptr, run_ns = 0, wait_ns = 0;

    if (!pass_filter(next_pid) && !pass_filter(prev_pid))
        return 0;

    if (prev_pid) {
        on_ptr = bpf_map_lookup_elem(&oncpu_ts, &prev_pid);
        if (on_ptr) run_ns = now - *on_ptr;
    }

    if (next_pid) {
        w_ptr = bpf_map_lookup_elem(&wake_ts, &next_pid);
        if (w_ptr) {
            wait_ns = now - *w_ptr;
            bpf_map_delete_elem(&wake_ts, &next_pid);
        }
        bpf_map_update_elem(&oncpu_ts, &next_pid, &now, BPF_ANY);
    }

    struct agg *ap = agg_touch(prev_pid);
    if (ap) { ap->total_run_ns += run_ns; ap->switches++; }
    struct agg *an = agg_touch(next_pid);
    if (an) { an->total_wait_ns += wait_ns; an->switches++; }

    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) return 0;
    e->ts_ns = now;
    e->type  = EV_SWITCH;
    e->pid   = next_pid;
    bpf_core_read_str(e->u.sw.prev_comm, sizeof(e->u.sw.prev_comm), &prev->comm);
    bpf_core_read_str(e->u.sw.next_comm, sizeof(e->u.sw.next_comm), &next->comm);
    e->u.sw.prev_pid = prev_pid;
    e->u.sw.next_pid = next_pid;
    e->u.sw.run_ns   = run_ns;
    e->u.sw.wait_ns  = wait_ns;
    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* ---- Task 8 fork handler ---- */
SEC("tp_btf/sched_process_fork")
int BPF_PROG(on_fork_btf, struct task_struct *parent, struct task_struct *child)
{
    __u64 now = bpf_ktime_get_ns();
    __u32 ppid = BPF_CORE_READ(parent, pid);
    __u32 cpid = BPF_CORE_READ(child, pid);

    if (!pass_filter(ppid))
        return 0;

    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
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
    e->u.sw.run_ns = 0;
    e->u.sw.wait_ns = 0;
    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* ---- existing exec / exit handlers unchanged ---- */
SEC("tp_btf/sched_process_exec")
int BPF_PROG(on_exec_btf, struct task_struct *p, pid_t old_pid, struct linux_binprm *bprm)
{
    __u64 now = bpf_ktime_get_ns();
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    if (!pass_filter(pid))
        return 0;
    struct agg *a = agg_touch(pid);
    if (a && a->exec_ts_ns == 0) a->exec_ts_ns = now;
    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) return 0;
    e->ts_ns = now;
    e->type  = EV_EXEC;
    e->pid   = pid;
    bpf_get_current_comm(e->comm, sizeof(e->comm));
    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_process_exit")
int BPF_PROG(on_exit_btf, struct task_struct *p)
{
    __u64 id = bpf_get_current_pid_tgid();
    __u32 pid = id >> 32, tid = (__u32)id;
    if (pid != tid || !pass_filter(pid))
        return 0;
    bpf_map_delete_elem(&wake_ts, &pid);
    bpf_map_delete_elem(&oncpu_ts, &pid);
    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) return 0;
    e->ts_ns = bpf_ktime_get_ns();
    e->type  = EV_EXIT;
    e->pid   = pid;
    bpf_get_current_comm(e->comm, sizeof(e->comm));
    bpf_ringbuf_submit(e, 0);
    return 0;
}
