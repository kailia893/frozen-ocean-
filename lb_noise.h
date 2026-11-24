#ifndef LB_NOISE_H
#define LB_NOISE_H
#include "cubiomes/biomenoise.h"

typedef struct {
    uint64_t *keys;
    int *values;
    unsigned int *hasvalue;
    int capacity;
    int cur;
    unsigned int generation;
    int used;
} hash_table;

typedef struct {
    uint64_t seed;
    unsigned int initialized;
    unsigned char initialized_octaves[6];
    DoublePerlinNoise climate[6];
    PerlinNoise octaves[40];
    hash_table hash;
    hash_table floodfill_hash;
} lbNoise;

void lb_init(lbNoise *noise, int capacity, int floodfill_max);
void lb_setseed(lbNoise *noise, uint64_t seed);
// capacity must be power of 2
double lb_octave(lbNoise *noise, int type, int octave_index, char half_octave, double x, double z);
int lb_octave_int(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z);
int lb_octave_int_uncached(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z);
int lb_octave_int_sum(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z);
void free_all(lbNoise *noise);

typedef int (*valid)(lbNoise *noise, int x, int z);
typedef struct { int x; int z; } pos;
int floodfill(lbNoise *noise, valid is_valid, int x, int z, int step_size, int limit);
int custom_floodfill(lbNoise *noise, valid is_valid, valid skip, valid stop, int x, int z, int step_size, int limit);
int auto_true(lbNoise *noise, int x, int z);
int auto_false(lbNoise *noise, int x, int z);

#endif
