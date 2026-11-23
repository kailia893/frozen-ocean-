#include "sb_noise.h"
#include "crunch.h"
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
// gcc main.c crunch.c sb_noise.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test

#define MAX_MATCHES 10000
#define REPORT_INTERVAL UINT64_C(100000000)
#define SEED_START UINT64_C(6767676767677676)
#define SEED_LIMIT UINT64_C(10000000000000000)
#define MIN_SIZE 30000
#define THREADS 8
enum { THREAD_COUNT = 8 };
time_t start;
typedef struct {
    atomic_uint_fast64_t checked;
    atomic_uint_fast64_t matches;
    atomic_bool stop;
    uint64_t end_seed;
    pthread_mutex_t report_mutex;
} Reporter;
typedef struct {
    Reporter *reporter;
    uint64_t thread_index;
} Worker;

int get_A_cont(sbNoise *noise, int x, int z) {
  int value = 0;
  value += sb_octave_int_uncached(noise, NP_CONTINENTALNESS, 0, 'A', x, z);
  value += sb_octave_int_uncached(noise, NP_CONTINENTALNESS, 1, 'A', x, z);
  value += sb_octave_int_uncached(noise, NP_CONTINENTALNESS, 2, 'A', x, z);
  //value += sb_octave_int_uncached(noise, NP_CONTINENTALNESS, 3, 'A', x, z);
  return value;
}

int get_A_temp(sbNoise *noise, int x, int z) {
  int value = 0;
  value += sb_octave_int_uncached(noise, NP_TEMPERATURE, 0, 'A', x, z);
  value += sb_octave_int_uncached(noise, NP_TEMPERATURE, 1, 'A', x, z);
  return value;
}

int good_A(sbNoise *noise, int x, int z) {
  int temp = get_A_temp(noise, x, z); 
  if (temp >= -4000) return 0;
  int cont = get_A_cont(noise, x, z);
  if (cont >= -1000) return 0;
  return 1;
}

int full_valid(sbNoise *noise, int x, int z) {
    int temp = sb_octave_int_sum(noise, NP_TEMPERATURE, 1, 'B', x, z);
    if (temp >= -4500) return 0;
    int cont = sb_octave_int_sum(noise, NP_CONTINENTALNESS, 3, 'B', x, z);
    if (cont <= -4500 || cont >= -2000) return 0;
    return 1;
}

int pass_seed(uint64_t seed, sbNoise* noise) {
    sb_setseed(noise, seed);
    // float crunchiness(sbNoise *noise, int type, int octave_index, char half_octave);
    float p = crunchiness(noise, NP_TEMPERATURE, 0, 'A');
    if (p > 0.25) {
        return 0;
    } 
    float q = crunchiness(noise, NP_TEMPERATURE, 0, 'B');
    if (p + q > 0.25) {
        return 0;
    }
    if (p + q + crunchiness(noise, NP_TEMPERATURE, 1, 'A') + crunchiness(noise, NP_TEMPERATURE, 1, 'B') > 0.2) {
        return 0;
    }
    int A_size = floodfill(noise, good_A, 0, 0, 256, 512);
    if (A_size < 380) {
        //printf("%lld\n",seed);
        return 0;
    }
    int tile = 1048576;
    int times = 28;
    for (int cx = -tile * times; cx <= tile * times; cx += tile){
        for (int cz = -tile * times; cz <= tile * times; cz += tile){
            int x = cx, z = cz;
            int size = floodfill(noise, full_valid, x, z, 32, 32768);
            if (size > MIN_SIZE){
                printf("at %d, %d               ", x, z);
                return size;
            }
        }
    }
    return 0;
}






static void *worker(void *arg) {
    Worker *args = arg;
    Reporter *reporter = args->reporter;
    sbNoise seeds;
    sb_init(&seeds, 2, 32768);
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
            double seconds = difftime(end, start);
            printf("seeds scanned %" PRIu64 "  Matches: %"PRIu64 "  time:  %.0lf\n",checked, atomic_load(&reporter->matches),seconds);
            pthread_mutex_unlock(&reporter->report_mutex);
        }
        if (reporter->end_seed - seed <= THREAD_COUNT) break;
        seed += THREAD_COUNT;
    }
    free_all(&seeds);
    return NULL;
}

int main() {
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
    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_mutex_destroy(&reporter.report_mutex);
    return 0;
}
