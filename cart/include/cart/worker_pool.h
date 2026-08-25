#ifndef CART_WORKER_POOL_H
#define CART_WORKER_POOL_H

#include <pthread.h>
#include <stdint.h>

/* Upper bound on pool size: four render workers on the PSTV today, with
 * headroom for future workers (e.g. upscale passes) without growing the
 * inline storage. */
#define CART_WORKER_POOL_MAX 8

typedef void (*cart_worker_job_fn)(void *slot, int row_start, int row_end,
                                   uint64_t generation);

struct cart_worker_pool;
struct cart_worker_start {
    struct cart_worker_pool *pool;
    int index;
};

/* Persistent render worker pool.
 *
 * Threads are created once at init and live for the process lifetime; each
 * dispatch fans a job out over contiguous row bands in lockstep via two
 * barriers, so a dispatch returns only after every band has completed.
 * Band boundaries are the legacy split: index * rows / worker_count.
 *
 * worker_count == 0 selects inline mode: dispatch runs the job once over the
 * full row range on the calling thread (used by the deterministic --dump
 * capture path, which must stay thread-free). */
struct cart_worker_pool {
    int active;
    int worker_count;
    int rows;
    pthread_t threads[CART_WORKER_POOL_MAX];
    struct cart_worker_start worker_starts[CART_WORKER_POOL_MAX];
    void *slots[CART_WORKER_POOL_MAX];
    cart_worker_job_fn job;
    uint64_t generation;
    pthread_barrier_t start_barrier;
    pthread_barrier_t stop_barrier;
    pthread_mutex_t startup_mutex;
    pthread_cond_t startup_cond;
    int startup_ready;
    int startup_released;
    int shutting_down;
};

/* worker_count must be in [0, CART_WORKER_POOL_MAX]; rows must be positive.
 * Returns 0 on success, -1 on invalid arguments or thread creation failure
 * (in which case any threads already started are joined before returning). */
int cart_worker_pool_init(struct cart_worker_pool *pool, int worker_count,
                          int rows);

/* Fan the job out over the pool. slots must reference worker_count objects
 * when worker_count > 0, or one object in inline mode. Returns 0 after all
 * bands completed, or -1 on invalid arguments / use after shutdown. */
int cart_worker_pool_dispatch(struct cart_worker_pool *pool,
                              cart_worker_job_fn job, void *const *slots,
                              uint64_t generation);

/* Signal the workers to exit and join them. Idempotent; safe on a pool that
 * failed init. */
void cart_worker_pool_shutdown(struct cart_worker_pool *pool);

#endif
