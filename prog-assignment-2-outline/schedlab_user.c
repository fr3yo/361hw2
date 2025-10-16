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
#include "schedlab.skel.h"

enum mode {
    MODE_STREAM = 0,
    MODE_LATENCY,
    MODE_FAIRNESS,
    MODE_CTX,
    MODE_TIMELINE,
    MODE_SHORTLONG,
    MODE_STARVATION,
    MODE_FORK              // Task 8 mode
};

static const char *mode_names[] = {
    "stream","latency","fairness","ctx","timeline","shortlong","starvation","fork"
};

enum ev_type {
    EV_WAKE     = 1,
    EV_SWITCH   = 2,
    EV_EXEC     = 3,
    EV_EXIT     = 4,
    EV_WAITLONG = 6,
    EV_FORK     = 7          // Task 8 event
};

/* ... your existing global vars and structs unchanged ... */

static void print_csv_header_once(void)
{
    if (!g_csv || !g_csv_header)
        return;

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
    case MODE_FORK:
        puts("ts_ns,parent_pid,child_pid");
        break;
    }

    fflush(stdout);
    g_csv_header = 0;
}

static int handle_event(void *ctx, void *data, size_t len)
{
    (void)ctx;
    const struct event *e = (const struct event *)data;

    print_csv_header_once();

    if (g_csv && g_mode == MODE_FORK && e->type == EV_FORK) {
        printf("%" PRIu64 ",%u,%u\n",
               (uint64_t)e->ts_ns,
               e->u.sw.prev_pid,
               e->u.sw.next_pid);
        fflush(stdout);
        return 0;
    }

    /* existing logic for other events remains intact */
    return 0;
}
