#include "lb_noise.h"
#include "crunch.h"
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
// gcc main.c lb_noise.c crunch.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test

#define MAX_MATCHES 10000
#define REPORT_INTERVAL UINT64_C(1000000000)
#define SEED_START UINT64_C(0)
#define SEED_LIMIT UINT64_C(10000000000000000)
#define MIN_SIZE 32000
#define THREADS 20
enum { THREAD_COUNT = 20 };
time_t start;
typedef struct {
    atomic_uint_fast64_t checked;
    atomic_uint_fast64_t matches;
    atomic_bool stop;
    uint64_t end_seed;
    pthread_mutex_t report_mutex;
} Reporter;
typedef struct { Reporter *reporter; uint64_t thread_index; } Worker;

int get_A_cont(lbNoise *noise, int x, int z) {
  int value = 0;
  value += lb_octave_int_uncached(noise, NP_CONTINENTALNESS, 0, 'A', x, z);
  value += lb_octave_int_uncached(noise, NP_CONTINENTALNESS, 1, 'A', x, z);
  value += lb_octave_int_uncached(noise, NP_CONTINENTALNESS, 2, 'A', x, z);
  return value;
}
int get_A_temp(lbNoise *noise, int x, int z) {
  int value = 0;
  value += lb_octave_int_uncached(noise, NP_TEMPERATURE, 0, 'A', x, z);
  value += lb_octave_int_uncached(noise, NP_TEMPERATURE, 1, 'A', x, z);
  return value;
}
int good_A(lbNoise *noise, int x, int z) {
  int temp = get_A_temp(noise, x, z);
  if (temp >= -4000) return 0;
  int cont = get_A_cont(noise, x, z);
  if (cont >= -1000) return 0;
  return 1;
}
int full_valid(lbNoise *noise, int x, int z) {
    int temp = lb_octave_int_sum(noise, NP_TEMPERATURE, 1, 'B', x, z);
    if (temp >= -4500) return 0;
    int cont = lb_octave_int_sum(noise, NP_CONTINENTALNESS, 3, 'B', x, z);
    if (cont <= -4500 || cont >= -2000) return 0;
    return 1;
}
int pass_seed(uint64_t seed, lbNoise *noise) {
    lb_setseed(noise, seed);
    float p = crunchiness(noise, NP_TEMPERATURE, 0, 'A');
    if (p > 0.25f) return 0;
    float q = crunchiness(noise, NP_TEMPERATURE, 0, 'B');
    if (p + q > 0.25f) return 0;
    if (p + q + crunchiness(noise, NP_TEMPERATURE, 1, 'A')
        + crunchiness(noise, NP_TEMPERATURE, 1, 'B') > 0.2f) return 0;
    int A_size = floodfill(noise, good_A, 0, 0, 1024, 512);
    if (A_size < 380) return 0;

    const int tile = 4194304;
    const int times = 7; // floor(30,000,000 / tile): maximum safe tiling radius
    for (int cx = -tile * times; cx <= tile * times; cx += tile) {
        for (int cz = -tile * times; cz <= tile * times; cz += tile) {
            int size = floodfill(noise, full_valid, cx, cz, 128, 32768);
            if (size > MIN_SIZE) {
                printf("at %d, %d               ", cx, cz);
                return size;
            }
        }
    }
    return 0;
}

static void *worker(void *arg) {
    Worker *args = arg;
    Reporter *reporter = args->reporter;
    lbNoise seeds;
    lb_init(&seeds, 2, 32768);
    for (uint64_t seed = SEED_START + args->thread_index;
         seed < reporter->end_seed && !atomic_load(&reporter->stop);) {
        int result = pass_seed(seed, &seeds);
        if (result > 0) {
            uint64_t match_number = atomic_fetch_add(&reporter->matches, 1) + 1;
            if (match_number <= MAX_MATCHES) {
                pthread_mutex_lock(&reporter->report_mutex);
                printf("seed  %" PRIu64 "\n", seed);
                pthread_mutex_unlock(&reporter->report_mutex);
            }
            if (match_number >= MAX_MATCHES) atomic_store(&reporter->stop, true);
        }
        uint64_t checked = atomic_fetch_add(&reporter->checked, 1) + 1;
        if (checked % REPORT_INTERVAL == 0) {
            pthread_mutex_lock(&reporter->report_mutex);
            time_t end = time(NULL);
            printf("seeds scanned %" PRIu64 "  Matches: %" PRIu64 "  time:  %.0lf\n",
                checked, atomic_load(&reporter->matches), difftime(end, start));
            pthread_mutex_unlock(&reporter->report_mutex);
        }
        if (reporter->end_seed - seed <= THREAD_COUNT) break;
        seed += THREAD_COUNT;
    }
    free_all(&seeds);
    return NULL;
}

int main(void) {
    start = time(NULL);
    Reporter reporter;
    atomic_init(&reporter.checked, 0);
    atomic_init(&reporter.matches, 0);
    atomic_init(&reporter.stop, false);
    pthread_mutex_init(&reporter.report_mutex, NULL);
    reporter.end_seed = SEED_START + SEED_LIMIT;
    pthread_t threads[THREADS];
    Worker args[THREADS];
    for (int i = 0; i < THREADS; i++) {
        args[i].reporter = &reporter;
        args[i].thread_index = (uint64_t)i;
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < THREADS; i++) pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&reporter.report_mutex);
    return 0;
}
