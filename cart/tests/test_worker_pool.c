#define _GNU_SOURCE
#include <cart/worker_pool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Records which rows each job touched so tests can assert exact band
 * coverage without depending on scheduling order. */
#define MAX_ROWS 256

struct band_recorder {
    int row_start;
    int row_end;
    int rows[MAX_ROWS];
    int touch_count;
    unsigned long long executions;
    uint64_t last_frame;
};

static void band_job(void *arg, int row_start, int row_end, uint64_t generation)
{
    struct band_recorder *recorder = arg;

    recorder->row_start = row_start;
    recorder->row_end = row_end;
    recorder->last_frame = generation;
    recorder->executions++;
    for (int row = row_start; row < row_end && row - row_start < MAX_ROWS; row++)
        recorder->rows[row] = 1;
    recorder->touch_count = row_end - row_start;
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
}

static void expect(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static void expect_band(struct band_recorder *recorder, int start, int end,
                        const char *message)
{
    char detail[128];

    if (recorder->row_start != start || recorder->row_end != end) {
        snprintf(detail, sizeof(detail), "%s (got %d..%d want %d..%d)",
                 message, recorder->row_start, recorder->row_end, start, end);
        fail(detail);
    }
}

static void test_init_validation(void)
{
    struct cart_worker_pool pool;

    expect(cart_worker_pool_init(NULL, 4, 4) == -1,
           "init rejects a NULL pool");
    expect(cart_worker_pool_init(&pool, 0, 0) == -1,
           "init rejects zero row count");
    expect(cart_worker_pool_init(&pool, CART_WORKER_POOL_MAX + 1, 4) == -1,
           "init rejects more workers than the pool supports");
}

static void test_inline_dispatch(void)
{
    struct cart_worker_pool pool;
    struct band_recorder recorder = {0};
    void *slots[1] = { &recorder };

    expect(cart_worker_pool_init(&pool, 0, 180) == 0,
           "inline pool initializes with zero workers");
    expect(cart_worker_pool_dispatch(&pool, band_job, slots,
                                     UINT64_C(120)) == 0,
           "inline dispatch succeeds");
    expect_band(&recorder, 0, 180, "inline job covers all rows");
    expect(recorder.executions == 1, "inline job executes exactly once");
    expect(recorder.last_frame == UINT64_C(120),
           "inline job observes the generation");
    cart_worker_pool_shutdown(&pool);
}

static void test_four_worker_bands(void)
{
    struct cart_worker_pool pool;
    struct band_recorder recorders[CART_WORKER_POOL_MAX] = {0};
    void *slots[CART_WORKER_POOL_MAX];
    int covered[MAX_ROWS] = {0};
    int total_rows = 0;

    for (int index = 0; index < 4; index++)
        slots[index] = &recorders[index];
    expect(cart_worker_pool_init(&pool, 4, 180) == 0,
           "four-worker pool initializes");
    expect(cart_worker_pool_dispatch(&pool, band_job, slots,
                                     UINT64_C(7)) == 0,
           "four-worker dispatch succeeds");
    for (int index = 0; index < 4; index++) {
        char message[64];

        snprintf(message, sizeof(message), "worker %d executed", index);
        expect(recorders[index].executions == 1, message);
    }
    /* Must match the legacy band split exactly: index * height / threads. */
    expect_band(&recorders[0], 0, 45, "band 0");
    expect_band(&recorders[1], 45, 90, "band 1");
    expect_band(&recorders[2], 90, 135, "band 2");
    expect_band(&recorders[3], 135, 180, "band 3");
    for (int index = 0; index < 4; index++) {
        for (int row = recorders[index].row_start;
             row < recorders[index].row_end; row++) {
            expect(row >= 0 && row < 180, "row in range");
            expect(covered[row] == 0, "no row covered twice");
            covered[row] = 1;
        }
        total_rows += recorders[index].touch_count;
    }
    expect(total_rows == 180, "bands tile the full height");
    cart_worker_pool_shutdown(&pool);
}

static void test_non_divisible_bands_tile(void)
{
    const int heights[] = { 7, 13, 181, 1 };
    const int workers[] = { 1, 3, 4, 7 };

    for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
        for (size_t w = 0; w < sizeof(workers) / sizeof(workers[0]); w++) {
            struct cart_worker_pool pool;
            struct band_recorder recorders[CART_WORKER_POOL_MAX] = {0};
            void *slots[CART_WORKER_POOL_MAX];
            int covered[MAX_ROWS] = {0};
            int height = heights[h];
            int worker_count = workers[w];
            int expected_start = 0;
            int total = 0;

            if (worker_count > CART_WORKER_POOL_MAX)
                continue;
            for (int index = 0; index < worker_count; index++)
                slots[index] = &recorders[index];
            expect(cart_worker_pool_init(&pool, worker_count, height) == 0,
                   "non-divisible pool initializes");
            expect(cart_worker_pool_dispatch(&pool, band_job, slots,
                                             UINT64_C(1)) == 0,
                   "non-divisible dispatch succeeds");
            for (int index = 0; index < worker_count; index++) {
                int start = index * height / worker_count;
                int end = (index + 1) * height / worker_count;

                expect_band(&recorders[index], start, end,
                            "non-divisible band matches legacy split");
                expect(recorders[index].row_start == expected_start,
                       "bands are contiguous");
                expected_start = recorders[index].row_end;
                for (int row = start; row < end; row++) {
                    expect(covered[row] == 0, "non-divisible rows covered once");
                    covered[row] = 1;
                }
                total += end - start;
            }
            expect(expected_start == height, "bands end at the height");
            expect(total == height, "non-divisible bands tile exactly");
            cart_worker_pool_shutdown(&pool);
        }
    }
}

static void test_sequential_dispatches_advance_generation(void)
{
    struct cart_worker_pool pool;
    struct band_recorder recorders[2] = {0};
    void *slots[2] = { &recorders[0], &recorders[1] };

    expect(cart_worker_pool_init(&pool, 2, 100) == 0,
           "sequential pool initializes");
    for (uint64_t generation = 0; generation < 8; generation++) {
        expect(cart_worker_pool_dispatch(&pool, band_job, slots,
                                         generation) == 0,
               "sequential dispatch succeeds");
        expect(recorders[0].executions == generation + 1,
               "each dispatch executes the job");
        expect(recorders[0].last_frame == generation,
               "job observes the latest generation");
        expect(recorders[1].last_frame == generation,
               "second worker observes the latest generation");
    }
    expect_band(&recorders[0], 0, 50, "sequential band 0");
    expect_band(&recorders[1], 50, 100, "sequential band 1");
    cart_worker_pool_shutdown(&pool);
}

static void test_dispatch_validation(void)
{
    struct cart_worker_pool pool;

    expect(cart_worker_pool_init(&pool, 2, 100) == 0, "pool initializes");
    expect(cart_worker_pool_dispatch(NULL, band_job, NULL, 0) == -1,
           "dispatch rejects a NULL pool");
    expect(cart_worker_pool_dispatch(&pool, NULL, NULL, 0) == -1,
           "dispatch rejects a NULL job");
    expect(cart_worker_pool_dispatch(&pool, band_job, NULL, 0) == -1,
           "threaded dispatch rejects NULL slots");
    cart_worker_pool_shutdown(&pool);
    expect(cart_worker_pool_dispatch(&pool, band_job, NULL, 0) == -1,
           "dispatch after shutdown fails closed");
}

int main(void)
{
    test_init_validation();
    test_inline_dispatch();
    test_four_worker_bands();
    test_non_divisible_bands_tile();
    test_sequential_dispatches_advance_generation();
    test_dispatch_validation();
    printf("worker pool tests passed\n");
    return 0;
}
