// schedlab/schedlab_user.c
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "schedlab.skel.h"   // generated from schedlab.bpf.o

/* ---- CLI modes (6 tasks only) ----------------------------------------- */
enum mode {
    MODE_STREAM = 0,   // raw event stream
    MODE_LATENCY,      // Task 1
    MODE_FAIRNESS,     // Task 2
    MODE_CTX,          // Task 3
    MODE_TIMELINE,     // Task 4
    MODE_SHORTLONG,    // Task 5
    MODE_STARVATION    // Task 6
};

static const char *mode_names[] = {
    "stream","latency","fairness","ctx","timeline","shortlong","starvation"
};

static enum mode parse_mode(const char *s) {
    for (int i=0;i<(int)(sizeof(mode_names)/sizeof(mode_names[0]));++i)
        if (strcmp(s, mode_names[i])==0) return (enum mode)i;
    return MODE_STREAM;
}

/* ---- Mirror BPF event types ------------------------------------------- */
enum ev_type {
    EV_WAKE     = 1,
    EV_SWITCH   = 2,
    EV_EXEC     = 3,
    EV_EXIT     = 4,
    EV_WAITLONG = 6,
};

struct ev_switch_payload {
    __u32 prev_pid, next_pid;
    char  prev_comm[16], next_comm[16];
    __u64 run_ns;
    __u64 wait_ns;
    __s32 prev_cpu, next_cpu;
};

struct event {
    __u64 ts_ns;
    __u32 type;
    __u32 pid;
    char  comm[16];
    union {
        struct ev_switch_payload  sw;
    } u;
};

/* This struct must match the one in schedlab.bpf.c */
struct cfg {
    __u64 wait_alert_ns;
    __u32 sample_filter_pid;
};

/* ---- Simple per-pid aggregates ---------------------------------------- */
struct agg_user {
    __u64 total_run_ns, total_wait_ns, switches, wakes;
    __u64 first_exec_ns, last_seen_ns;
};
#define HSIZE 65536
static struct agg_user agg_tbl[HSIZE];
static inline struct agg_user* A(__u32 pid) { return &agg_tbl[pid % HSIZE]; }

/* ---- Globals ----------------------------------------------------------- */
static volatile sig_atomic_t g_stop = 0;
static enum mode  g_mode = MODE_STREAM;
static int        g_csv = 0;
static int        g_csv_header = 0;
static __u32      g_filter_pid = 0;
static __u64      g_wait_alert_ns = 5ULL * 1000 * 1000; // 5ms default

static void on_sig(int sig) { (void)sig; g_stop = 1; }

/* ---- CSV header printer ----------------------------------------------- */
static void print_csv_header_once(void) {
    if (!g_csv || !g_csv_header) return;
    switch (g_mode) {
    case MODE_STREAM:
        puts("ts_ns,type,pid,comm,prev_pid,next_pid,run_ns,wait_ns");
        break;
    case MODE_LATENCY:
        puts("ts_ns,pid,latency_ns");
        break;
    case MODE_FAIRNESS:
        puts("pid,run_ms,wait_ms,switches");
        break;
    case MODE_CTX:
        puts("ts_ns,prev_pid,next_pid,run_ns");
        break;
    case MODE_TIMELINE:
        puts("ts_ns,pid,event,wait_ns,run_prev_ns");
        break;
    case MODE_SHORTLONG:
        puts("pid,lifetime_ms,wakes,switches");
        break;
    case MODE_STARVATION:
        puts("ts_ns,pid,event");
        break;
    }
    fflush(stdout);
    g_csv_header = 0;
}

/* ---- Ring buffer callback --------------------------------------------- */
static int handle_event(void *ctx, void *data, size_t len)
{
    (void)ctx;
    const struct event *e = (const struct event *)data;

    /* maintain small local aggregates */
    if (e->type == EV_EXEC) {
        if (A(e->pid)->first_exec_ns == 0) A(e->pid)->first_exec_ns = e->ts_ns;
    } else if (e->type == EV_SWITCH) {
        A(e->u.sw.prev_pid)->total_run_ns  += e->u.sw.run_ns;
        A(e->u.sw.next_pid)->total_wait_ns += e->u.sw.wait_ns;
        A(e->u.sw.prev_pid)->switches++;
        A(e->u.sw.next_pid)->switches++;
    } else if (e->type == EV_WAKE) {
        A(e->pid)->wakes++;
    }
    A(e->pid)->last_seen_ns = e->ts_ns;

    print_csv_header_once();

    if (!g_csv) {
        /* human-readable */
        switch (g_mode) {
        case MODE_STREAM:
            switch (e->type) {
            case EV_WAKE:
                fprintf(stdout, "[wake] pid=%u comm=%s\n", e->pid, e->comm); break;
            case EV_SWITCH:
                fprintf(stdout, "[switch] prev=%u(%s) -> next=%u(%s) run=%" PRIu64 "ns wait=%" PRIu64 "ns\n",
                    e->u.sw.prev_pid, e->u.sw.prev_comm,
                    e->u.sw.next_pid, e->u.sw.next_comm,
                    (uint64_t)e->u.sw.run_ns, (uint64_t)e->u.sw.wait_ns); break;
            case EV_EXEC:
                fprintf(stdout, "[exec] pid=%u comm=%s\n", e->pid, e->comm); break;
            case EV_EXIT:
                fprintf(stdout, "[exit] pid=%u comm=%s\n", e->pid, e->comm); break;
            case EV_WAITLONG:
                fprintf(stdout, "[wait-alert] pid=%u comm=%s\n", e->pid, e->comm); break;
            }
            break;

        case MODE_LATENCY:
            if (e->type == EV_SWITCH)
                fprintf(stdout, "latency_ns pid=%u value=%" PRIu64 "\n",
                    e->u.sw.next_pid, (uint64_t)e->u.sw.wait_ns);
            break;

        case MODE_FAIRNESS:
            if (e->type == EV_SWITCH) {
                const struct agg_user *an = A(e->u.sw.next_pid);
                fprintf(stdout, "fair pid=%u run_ms=%.6f wait_ms=%.6f switches=%" PRIu64 "\n",
                    e->u.sw.next_pid,
                    an->total_run_ns/1e6, an->total_wait_ns/1e6,
                    (uint64_t)an->switches);
            }
            break;

        case MODE_CTX:
            if (e->type == EV_SWITCH)
                fprintf(stdout, "ctxswitch prev=%u next=%u run_ns=%" PRIu64 "\n",
                    e->u.sw.prev_pid, e->u.sw.next_pid, (uint64_t)e->u.sw.run_ns);
            break;

        case MODE_TIMELINE:
            if (e->type == EV_WAKE)      fprintf(stdout, "T %u WAKE\n", e->pid);
            else if (e->type == EV_SWITCH)
                fprintf(stdout, "T %u SWITCH wait=%" PRIu64 " run_prev=%" PRIu64 "\n",
                    e->u.sw.next_pid, (uint64_t)e->u.sw.wait_ns, (uint64_t)e->u.sw.run_ns);
            else if (e->type == EV_EXEC) fprintf(stdout, "T %u EXEC\n", e->pid);
            else if (e->type == EV_EXIT) fprintf(stdout, "T %u EXIT\n", e->pid);
            break;

        case MODE_SHORTLONG:
            if (e->type == EV_EXIT) {
                const struct agg_user *ax = A(e->pid);
                __u64 life = (ax->last_seen_ns > ax->first_exec_ns)
                           ? (ax->last_seen_ns - ax->first_exec_ns) : 0;
                fprintf(stdout, "lifetime pid=%u ms=%.6f wakes=%" PRIu64 " switches=%" PRIu64 "\n",
                    e->pid, life/1e6, (uint64_t)ax->wakes, (uint64_t)ax->switches);
            }
            break;

        case MODE_STARVATION:
            if (e->type == EV_WAITLONG)
                fprintf(stdout, "starvation_alert pid=%u\n", e->pid);
            break;
        }
        fflush(stdout);
        return 0;
    }

    /* CSV mode */
    switch (g_mode) {
    case MODE_STREAM:
        if (e->type == EV_SWITCH) {
            printf("%" PRIu64 ",switch,%u,%s,%u,%u,%" PRIu64 ",%" PRIu64 "\n",
                (uint64_t)e->ts_ns, e->pid, e->comm,
                e->u.sw.prev_pid, e->u.sw.next_pid,
                (uint64_t)e->u.sw.run_ns, (uint64_t)e->u.sw.wait_ns);
        } else if (e->type == EV_WAKE) {
            printf("%" PRIu64 ",wake,%u,%s,,,%s,%s\n",
                (uint64_t)e->ts_ns, e->pid, e->comm, "", "");
        } else if (e->type == EV_EXEC) {
            printf("%" PRIu64 ",exec,%u,%s,,,%s,%s\n",
                (uint64_t)e->ts_ns, e->pid, e->comm, "", "");
        } else if (e->type == EV_EXIT) {
            printf("%" PRIu64 ",exit,%u,%s,,,%s,%s\n",
                (uint64_t)e->ts_ns, e->pid, e->comm, "", "");
        } else if (e->type == EV_WAITLONG) {
            printf("%" PRIu64 ",wait_alert,%u,%s,,,%s,%s\n",
                (uint64_t)e->ts_ns, e->pid, e->comm, "", "");
        }
        break;

    case MODE_LATENCY:
        if (e->type == EV_SWITCH)
            printf("%" PRIu64 ",%u,%" PRIu64 "\n",
                (uint64_t)e->ts_ns, e->u.sw.next_pid, (uint64_t)e->u.sw.wait_ns);
        break;

    case MODE_FAIRNESS:
        if (e->type == EV_SWITCH) {
            const struct agg_user *an = A(e->u.sw.next_pid);
            printf("%u,%.6f,%.6f,%" PRIu64 "\n",
                e->u.sw.next_pid, an->total_run_ns/1e6,
                an->total_wait_ns/1e6, (uint64_t)an->switches);
        }
        break;

    case MODE_CTX:
        if (e->type == EV_SWITCH)
            printf("%" PRIu64 ",%u,%u,%" PRIu64 "\n",
                (uint64_t)e->ts_ns, e->u.sw.prev_pid, e->u.sw.next_pid,
                (uint64_t)e->u.sw.run_ns);
        break;

    case MODE_TIMELINE:
        if (e->type == EV_WAKE)
            printf("%" PRIu64 ",%u,WAKE,,\n", (uint64_t)e->ts_ns, e->pid);
        else if (e->type == EV_SWITCH)
            printf("%" PRIu64 ",%u,SWITCH,%" PRIu64 ",%" PRIu64 "\n",
                (uint64_t)e->ts_ns, e->u.sw.next_pid,
                (uint64_t)e->u.sw.wait_ns, (uint64_t)e->u.sw.run_ns);
        else if (e->type == EV_EXEC)
            printf("%" PRIu64 ",%u,EXEC,,\n", (uint64_t)e->ts_ns, e->pid);
        else if (e->type == EV_EXIT)
            printf("%" PRIu64 ",%u,EXIT,,\n", (uint64_t)e->ts_ns, e->pid);
        break;

    case MODE_SHORTLONG:
        if (e->type == EV_EXIT) {
            const struct agg_user *ax = A(e->pid);
            __u64 life = (ax->last_seen_ns > ax->first_exec_ns)
                       ? (ax->last_seen_ns - ax->first_exec_ns) : 0;
            printf("%u,%.6f,%" PRIu64 ",%" PRIu64 "\n",
                e->pid, life/1e6, (uint64_t)ax->wakes, (uint64_t)ax->switches);
        }
        break;

    case MODE_STARVATION:
        if (e->type == EV_WAITLONG)
            printf("%" PRIu64 ",%u,wait_alert\n", (uint64_t)e->ts_ns, e->pid);
        break;
    }
    fflush(stdout);
    return 0;
}

/* ---- CLI & main ------------------------------------------------------- */
static void usage(const char *p) {
    fprintf(stderr,
        "Usage: sudo %s [--mode %s|%s|%s|%s|%s|%s|%s]\n"
        "              [--filter-pid N] [--wait-alert-ms M] [--csv] [--csv-header]\n",
        p,
        mode_names[0], mode_names[1], mode_names[2], mode_names[3],
        mode_names[4], mode_names[5], mode_names[6]);

}

int main(int argc, char **argv)
{
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"--mode") && i+1<argc) g_mode = parse_mode(argv[++i]);
        else if (!strcmp(argv[i],"--filter-pid") && i+1<argc) g_filter_pid = (__u32)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--wait-alert-ms") && i+1<argc) g_wait_alert_ns = (__u64)atoll(argv[++i]) * 1000000ULL;
        else if (!strcmp(argv[i],"--csv")) g_csv = 1;
        else if (!strcmp(argv[i],"--csv-header")) g_csv_header = 1;
        else { usage(argv[0]); return 1; }
    }

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    /* open + load the BPF object via skeleton */
    struct schedlab_bpf *skel = schedlab_bpf__open_and_load();
    if (!skel) { perror("open_and_load"); return 2; }

    /* init cfg_map in kernel */
    struct cfg c = {.wait_alert_ns = g_wait_alert_ns, .sample_filter_pid = g_filter_pid};
    __u32 k = 0;
    if (bpf_map_update_elem(bpf_map__fd(skel->maps.cfg_map), &k, &c, BPF_ANY)) {
        perror("bpf_map_update_elem(cfg_map)");
        schedlab_bpf__destroy(skel);
        return 3;
    }

    /* attach all tp_btf programs */
    if (schedlab_bpf__attach(skel)) {
        perror("attach");
        schedlab_bpf__destroy(skel);
        return 4;
    }

    /* ring buffer reader */
    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.rb),
                                              handle_event, NULL, NULL);
    if (!rb) {
        perror("ring_buffer__new");
        schedlab_bpf__destroy(skel);
        return 5;
    }

    if (!g_csv)
        fprintf(stderr, "schedlab attached. mode=%s filter-pid=%u wait-alert-ms=%" PRIu64 "\n",
            mode_names[g_mode], g_filter_pid, (uint64_t)(g_wait_alert_ns/1000000ULL));
    else
        print_csv_header_once();

    while (!g_stop) {
        int err = ring_buffer__poll(rb, 200);
        if (err == -EINTR) break;
        if (err < 0 && err != -EAGAIN) {
            fprintf(stderr, "ring_buffer__poll: %d\n", err);
            break;
        }
    }

    ring_buffer__free(rb);
    schedlab_bpf__destroy(skel);
    return 0;
}
