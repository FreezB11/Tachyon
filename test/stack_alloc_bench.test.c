#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <tachyon.h>

/* worker does nothing — we're measuring pure create/join overhead,
 * i.e. t_create -> (malloc or marena_alloc) -> ctx_switch -> exit -> join -> t_destroy */
void worker(void* arg) {
    (void)arg;
}

static double elapsed_ms(struct timespec start, struct timespec end) {
    double s  = (double)(end.tv_sec  - start.tv_sec);
    double ns = (double)(end.tv_nsec - start.tv_nsec);
    return (s * 1000.0) + (ns / 1e6);
}

/* Phase 1: create N threads, join each right away (create+drop, one at a time) */
static void phase_create_drop(int n) {
    for (int i = 0; i < n; i++) {
        thread_t* t = tachyon_thread(worker, NULL);
        tachyon_join(t);
    }
}

/* Phase 2: randomly create or join-oldest-pending, simulating churn.
 * Keeps at most `cap` threads alive at once so we don't overflow
 * the 64-chunk slab if USE_MARENA=1. */
static void phase_random_churn(int total_ops, int cap) {
    thread_t* pending[256];
    int count = 0;

    for (int i = 0; i < total_ops; i++) {
        int want_create = (count == 0) ? 1 :
                           (count >= cap) ? 0 :
                           (rand() % 2);

        if (want_create) {
            pending[count++] = tachyon_thread(worker, NULL);
        } else /*  */{
            /* join the oldest pending thread, then compact */
            tachyon_join(pending[0]);
            for (int j = 1; j < count; j++)
                pending[j - 1] = pending[j];
            count--;
        }
    }

    /* drain whatever is left */
    for (int i = 0; i < count; i++)
        tachyon_join(pending[i]);
}

/* Phase 3: create M threads, then kill (join) them all at the end */
static void phase_create_all_then_kill(int n, int cap) {
    thread_t* live[256];
    int created = 0;

    while (created < n) {
        int batch = (n - created < cap) ? (n - created) : cap;

        for (int i = 0; i < batch; i++)
            live[i] = tachyon_thread(worker, NULL);

        for (int i = 0; i < batch; i++)
            tachyon_join(live[i]);

        created += batch;
    }
}

int main() {
    tachyon_init();

    const int N_CREATE_DROP = 300;
    const int N_CHURN_OPS   = 500;
    const int N_KILL_ALL    = 300;
    const int CAP           = 60; /* stay under the 64-chunk slab limit */

    struct timespec t0, t1, t2, t3;

    printf("=== tachyon stack allocator benchmark ===\n");
#if USE_MARENA
    printf("allocator: marena (marena_alloc)\n");
#else
    printf("allocator: malloc\n");
#endif

    clock_gettime(CLOCK_MONOTONIC, &t0);
    phase_create_drop(N_CREATE_DROP);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("phase 1 [create+drop x%d]:      %.3f ms\n",
           N_CREATE_DROP, elapsed_ms(t0, t1));

    phase_random_churn(N_CHURN_OPS, CAP);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    printf("phase 2 [random churn x%d ops]: %.3f ms\n",
           N_CHURN_OPS, elapsed_ms(t1, t2));

    phase_create_all_then_kill(N_KILL_ALL, CAP);
    clock_gettime(CLOCK_MONOTONIC, &t3);
    printf("phase 3 [create %d, kill all]:  %.3f ms\n",
           N_KILL_ALL, elapsed_ms(t2, t3));

    printf("total:                          %.3f ms\n", elapsed_ms(t0, t3));

    return 0;
}