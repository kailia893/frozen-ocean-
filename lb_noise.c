#include "lb_noise.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Periods in the sampler's x/z coordinates (world coordinates divided by 4).
static const int period_a[6] = {
    4096 * 256, 1024 * 256, 2048 * 256,
    2048 * 256, 0, 128 * 256
};

static uint64_t hash_key(int type, int octave_index, char half_octave, int x, int z) {
    x += 30000000; z += 30000000;
    uint64_t tot = ((uint64_t)x << 38) | ((uint64_t)z << 12) | (type << 6)
        | (octave_index << 2) | (half_octave == 'A');
    tot *= 0x9E3779B97F4A7C15ULL;
    tot ^= (tot << 33); tot *= 0xFF51AFD7ED558CCDULL;
    tot ^= (tot >> 27); tot *= 0x94D049BB133111EBULL;
    tot ^= (tot >> 31);
    return tot;
}

static uint64_t pos_hash(pos cur) {
    cur.x += 30000000; cur.z += 30000000;
    uint64_t tot = ((uint64_t)cur.x << 32) | (cur.z);
    tot *= 0x9E3779B97F4A7C15ULL;
    tot ^= (tot << 33); tot *= 0xFF51AFD7ED558CCDULL;
    tot ^= (tot >> 27); tot *= 0x94D049BB133111EBULL;
    tot ^= (tot >> 31);
    return tot;
}

static void init_hash(hash_table *table, int capacity) {
    table->capacity = capacity; table->cur = 0;
    table->keys = malloc((size_t)capacity * sizeof(*table->keys));
    table->values = malloc((size_t)capacity * sizeof(*table->values));
    table->hasvalue = calloc((size_t)capacity, sizeof(*table->hasvalue));
    table->generation = 1; table->used = 0;
}

static void setvalue(hash_table *table, uint64_t key, int value) {
    table->used = 1;
    if (table->cur + 1 > table->capacity) return;
    uint64_t mask = (uint64_t)table->capacity - 1;
    int index = (int)(key & mask);
    for (int i = 0; i < table->capacity; i++) {
        if (table->hasvalue[index] != table->generation) {
            table->keys[index] = key; table->values[index] = value;
            table->hasvalue[index] = table->generation; table->cur++;
            return;
        }
        index = (index + 1) & (int)mask;
    }
}

static int getvalue(hash_table *table, uint64_t key) {
    uint64_t mask = (uint64_t)table->capacity - 1;
    int index = (int)(key & mask);
    for (int i = 0; i < table->capacity; i++) {
        if (table->hasvalue[index] != table->generation) return 1 << 30;
        if (table->keys[index] == key) return table->values[index];
        index = (index + 1) & (int)mask;
    }
    return 1 << 30;
}

static void rehash(hash_table *table) {
    if (!table->used) return;
    if (table->generation == UINT_MAX) {
        memset(table->hasvalue, 0, (size_t)table->capacity * sizeof(*table->hasvalue));
        table->generation = 1;
    } else table->generation++;
    table->cur = 0; table->used = 0;
}

static void free_hash_table(hash_table *table) {
    free(table->keys); free(table->values); free(table->hasvalue);
    table->keys = NULL; table->values = NULL; table->hasvalue = NULL;
    table->capacity = 0; table->cur = 0; table->generation = 0; table->used = 0;
}

static const int octave_counts[6] = {2, 2, 9, 4, 0, 3};

void lb_init(lbNoise *noise, int capacity, int floodfill_max) {
    noise->initialized = 0;
    memset(noise->initialized_octaves, 0, sizeof(noise->initialized_octaves));
    init_hash(&noise->hash, capacity);
    init_hash(&noise->floodfill_hash, floodfill_max * 4);
}

void lb_setseed(lbNoise *noise, uint64_t seed) {
    noise->seed = seed; noise->initialized = 0;
    memset(noise->initialized_octaves, 0, sizeof(noise->initialized_octaves));
    rehash(&noise->hash); rehash(&noise->floodfill_hash);
}

static void init_parameter(lbNoise *noise, int type, int octaves) {
    BiomeNoise b = {0};
    int offset = 0;
    for (int i = NP_TEMPERATURE; i < type; i++) offset += 2 * octave_counts[i];
    // large=1 selects Cubiomes' large-biome salts and octave ranges.
    setClimateParaSeed(&b, noise->seed, 1, type, 2 * octaves);
    DoublePerlinNoise *d = &b.climate[type];
    int count = d->octA.octcnt + d->octB.octcnt;
    memcpy(noise->octaves + offset, b.oct, (size_t)count * sizeof(*noise->octaves));
    noise->climate[type] = *d;
    noise->climate[type].octA.octaves = noise->octaves + offset;
    noise->climate[type].octB.octaves = noise->octaves + offset + d->octA.octcnt;
    noise->initialized |= 1u << type;
    noise->initialized_octaves[type] = (unsigned char)d->octA.octcnt;
}

static void init(lbNoise *noise, int type, int octaves) {
    if (!(noise->initialized & (1u << type)) || noise->initialized_octaves[type] < octaves)
        init_parameter(noise, type, octaves);
}

static inline void part(const DoublePerlinNoise *d, int octave_index, char half_octave,
    double x, double z, double *value) {
    const OctaveNoise *s;
    double mult;
    if (half_octave == 'A') { s = &d->octA; mult = 1.0; }
    else { s = &d->octB; mult = 337.0 / 331.0; }
    const PerlinNoise *q = s->octaves + octave_index;
    *value = d->amplitude * q->amplitude * samplePerlin(q,
        x * mult * q->lacunarity, 0, z * mult * q->lacunarity, 0, 0);
}

double lb_octave(lbNoise *noise, int type, int octave_index, char half_octave, double x, double z) {
    double value;
    init(noise, type, octave_index + 1);
    part(&noise->climate[type], octave_index, half_octave, x, z, &value);
    return value;
}

int lb_octave_int(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z) {
    x /= 4; z /= 4;
    if (half_octave == 'A') {
        x = ((x % period_a[type]) + period_a[type]) % period_a[type];
        z = ((z % period_a[type]) + period_a[type]) % period_a[type];
    }
    uint64_t key = hash_key(type, octave_index, half_octave, x, z);
    int val = getvalue(&noise->hash, key);
    if (val != (1 << 30)) return val;
    int res = (int)(lb_octave(noise, type, octave_index, half_octave, x, z) * 10000);
    setvalue(&noise->hash, key, res);
    return res;
}

int lb_octave_int_uncached(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z) {
    x /= 4; z /= 4;
    return (int)(lb_octave(noise, type, octave_index, half_octave, x, z) * 10000);
}

int lb_octave_int_sum(lbNoise *noise, int type, int octave_index, char half_octave, int x, int z) {
    int sum = 0;
    for (int i = 0; i <= octave_index; i++) {
        if (i == octave_index && half_octave == 'A')
            sum += lb_octave_int_uncached(noise, type, i, 'A', x, z);
        else {
            sum += lb_octave_int_uncached(noise, type, i, 'A', x, z);
            sum += lb_octave_int_uncached(noise, type, i, 'B', x, z);
        }
    }
    return sum;
}

int floodfill(lbNoise *noise, valid is_valid, int x, int z, int step_size, int limit) {
    hash_table *visited = &noise->floodfill_hash;
    rehash(visited);
    if (!is_valid(noise, x, z)) return 0;
    pos q[limit]; int head = 0, tail = 0;
    int dx[] = {-step_size, step_size, 0, 0};
    int dz[] = {0, 0, -step_size, step_size};
    q[head++] = (pos){x, z}; setvalue(visited, pos_hash((pos){x, z}), 1);
    while (head > tail) {
        pos cur = q[tail++];
        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dx[i], nz = cur.z + dz[i];
            if (getvalue(visited, pos_hash((pos){nx, nz})) != (1 << 30)) continue;
            if (is_valid(noise, nx, nz)) {
                if (head >= limit || tail >= limit) return limit;
                setvalue(visited, pos_hash((pos){nx, nz}), 1); q[head++] = (pos){nx, nz};
            } else setvalue(visited, pos_hash((pos){nx, nz}), 1);
        }
    }
    return head;
}

int custom_floodfill(lbNoise *noise, valid is_valid, valid skip, valid stop,
    int x, int z, int step_size, int limit) {
    hash_table *visited = &noise->floodfill_hash;
    rehash(visited);
    if (!is_valid(noise, x, z)) return 0;
    pos q[limit]; int head = 0, tail = 0, sub = 0;
    int dx[] = {-step_size, step_size, 0, 0};
    int dz[] = {0, 0, -step_size, step_size};
    q[head++] = (pos){x, z}; setvalue(visited, pos_hash((pos){x, z}), 1);
    while (head > tail) {
        pos cur = q[tail++];
        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dx[i], nz = cur.z + dz[i];
            if (getvalue(visited, pos_hash((pos){nx, nz})) != (1 << 30)) continue;
            if (is_valid(noise, nx, nz)) {
                if (skip(noise, nx, nz)) sub++;
                if (head >= limit || tail >= limit) return limit;
                setvalue(visited, pos_hash((pos){nx, nz}), 1); q[head++] = (pos){nx, nz};
            } else {
                setvalue(visited, pos_hash((pos){nx, nz}), 1);
                if (stop(noise, nx, nz)) return 0;
            }
        }
    }
    return head - sub;
}

int auto_true(lbNoise *noise, int x, int z) { (void)noise; (void)x; (void)z; return true; }
int auto_false(lbNoise *noise, int x, int z) { (void)noise; (void)x; (void)z; return false; }
void free_all(lbNoise *noise) { free_hash_table(&noise->hash); free_hash_table(&noise->floodfill_hash); }
