/* pthread_barrier_* requires _POSIX_C_SOURCE >= 200112L; every TU that
 * includes this header must define _GNU_SOURCE (or equivalent) first. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cart/worker_pool.h>

#include <string.h>

/* Sense-reversing barrier over two barrier objects: the pool's own workers
 * plus the dispatching thread, so dispatch returning == all bands done.
 *
 * Memory ordering: pthread_barrier_wait provides the necessary release
 * (arrive) / acquire (leave) pairs, so job/slots/generation writes issued
 * before the start barrier are visible to every worker after it, and worker
 * writes complete before dispatch returns through the stop barrier. */

static void *worker_main(void *arg)
{
    struct cart_worker_start *start = arg;
    struct cart_worker_pool *pool = start->pool;
    int worker_index = start->index;

    pthread_mutex_lock(&pool->startup_mutex);
    while (!pool->startup_ready)
        pthread_cond_wait(&pool->startup_cond, &pool->startup_mutex);
    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->startup_mutex);
        return NULL;
    }
    pool->startup_released++;
    pthread_cond_broadcast(&pool->startup_cond);
    pthread_mutex_unlock(&pool->startup_mutex);

    for (;;) {
        pthread_barrier_wait(&pool->start_barrier);
        if (pool->shutting_down)
            return NULL;
        {
            int start = worker_index * pool->rows / pool->worker_count;
            int end = (worker_index + 1) * pool->rows / pool->worker_count;

            pool->job(pool->slots[worker_index], start, end,
                      pool->generation);
        }
        pthread_barrier_wait(&pool->stop_barrier);
    }
}

int cart_worker_pool_init(struct cart_worker_pool *pool, int worker_count,
                          int rows)
{
    if (pool == NULL || worker_count < 0 ||
        worker_count > CART_WORKER_POOL_MAX || rows <= 0)
        return -1;

    memset(pool, 0, sizeof(*pool));
    pool->worker_count = worker_count;
    pool->rows = rows;

    if (worker_count == 0) {
        pool->active = 1;
        return 0;
    }

    if (pthread_mutex_init(&pool->startup_mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&pool->startup_cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->startup_mutex);
        return -1;
    }

    if (pthread_barrier_init(&pool->start_barrier, NULL,
                             (unsigned)worker_count + 1) != 0) {
        pthread_cond_destroy(&pool->startup_cond);
        pthread_mutex_destroy(&pool->startup_mutex);
        return -1;
    }
    if (pthread_barrier_init(&pool->stop_barrier, NULL,
                             (unsigned)worker_count + 1) != 0) {
        pthread_barrier_destroy(&pool->start_barrier);
        pthread_cond_destroy(&pool->startup_cond);
        pthread_mutex_destroy(&pool->startup_mutex);
        return -1;
    }

    for (int index = 0; index < worker_count; index++) {
        struct cart_worker_start *start = &pool->worker_starts[index];
        start->pool = pool;
        start->index = index;
        if (pthread_create(&pool->threads[index], NULL, worker_main,
                           start) != 0) {
            /* Fail closed: release only the workers that were actually
             * created through the startup gate, then join them. They have
             * not entered the render barriers yet. */
            pthread_mutex_lock(&pool->startup_mutex);
            pool->shutting_down = 1;
            pool->startup_ready = 1;
            pthread_cond_broadcast(&pool->startup_cond);
            pthread_mutex_unlock(&pool->startup_mutex);
            for (int started = 0; started < index; started++)
                pthread_join(pool->threads[started], NULL);
            pthread_barrier_destroy(&pool->start_barrier);
            pthread_barrier_destroy(&pool->stop_barrier);
            pthread_cond_destroy(&pool->startup_cond);
            pthread_mutex_destroy(&pool->startup_mutex);
            pool->worker_count = 0;
            pool->rows = 0;
            return -1;
        }
    }
    pthread_mutex_lock(&pool->startup_mutex);
    pool->startup_ready = 1;
    pthread_cond_broadcast(&pool->startup_cond);
    while (pool->startup_released < worker_count)
        pthread_cond_wait(&pool->startup_cond, &pool->startup_mutex);
    pthread_mutex_unlock(&pool->startup_mutex);
    pool->active = 1;
    return 0;
}

int cart_worker_pool_dispatch(struct cart_worker_pool *pool,
                              cart_worker_job_fn job, void *const *slots,
                              uint64_t generation)
{
    if (pool == NULL || job == NULL || !pool->active || slots == NULL)
        return -1;

    if (pool->worker_count == 0) {
        job(slots[0], 0, pool->rows, generation);
        return 0;
    }

    pool->job = job;
    memcpy(pool->slots, slots, sizeof(void *) * (size_t)pool->worker_count);
    pool->generation = generation;

    pthread_barrier_wait(&pool->start_barrier);
    pthread_barrier_wait(&pool->stop_barrier);
    return 0;
}

void cart_worker_pool_shutdown(struct cart_worker_pool *pool)
{
    if (pool == NULL || !pool->active)
        return;

    pool->active = 0;
    if (pool->worker_count == 0)
        return;

    pool->shutting_down = 1;
    pthread_barrier_wait(&pool->start_barrier);
    for (int index = 0; index < pool->worker_count; index++)
        pthread_join(pool->threads[index], NULL);
    pthread_barrier_destroy(&pool->start_barrier);
    pthread_barrier_destroy(&pool->stop_barrier);
    pthread_cond_destroy(&pool->startup_cond);
    pthread_mutex_destroy(&pool->startup_mutex);
    pool->worker_count = 0;
    pool->rows = 0;
}
