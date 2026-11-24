/* Standalone verifier for the equal-weight signed large-biome temperature crunch sum.
 * Build: cc -std=c11 -O2 temperature_crunch_check.c -lm -o temperature_crunch_check
 * Run:   ./temperature_crunch_check
 */
#include <stdint.h>
#include <stdio.h>

typedef struct { uint64_t lo, hi; } Xoroshiro;
typedef struct { uint64_t lo, hi; } SeedPair;

static const SeedPair climate_salt = {0x944b0073edf549dbULL, 0x4ff44347e9d22b96ULL};
/* Large temperature octave -12 and -10 map to Cubiomes octave salts 4 and 6. */
static const SeedPair octave_salts[2] = {
    {0xb198de63a8012672ULL, 0x7b84cad43ef7b5a8ULL},
    {0x36d326eed40efeb2ULL, 0x5be9ce18223c636aULL}
};

static uint64_t rotl64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static void xSetSeed(Xoroshiro *xr, uint64_t value) {
    const uint64_t XL = 0x9e3779b97f4a7c15ULL, XH = 0x6a09e667f3bcc909ULL;
    const uint64_t A = 0xbf58476d1ce4e5b9ULL, B = 0x94d049bb133111ebULL;
    uint64_t l = value ^ XH, h = l + XL;
    l = (l ^ (l >> 30)) * A; h = (h ^ (h >> 30)) * A;
    l = (l ^ (l >> 27)) * B; h = (h ^ (h >> 27)) * B;
    xr->lo = l ^ (l >> 31); xr->hi = h ^ (h >> 31);
}

static uint64_t xNextLong(Xoroshiro *xr) {
    uint64_t l = xr->lo, h = xr->hi, n = rotl64(l + h, 17) + l;
    h ^= l; xr->lo = rotl64(l, 49) ^ h ^ (h << 21); xr->hi = rotl64(h, 28);
    return n;
}

static SeedPair half_seed(uint64_t seed, char half) {
    Xoroshiro rng; xSetSeed(&rng, seed);
    uint64_t lo = xNextLong(&rng), hi = xNextLong(&rng);
    rng.lo = lo ^ climate_salt.lo; rng.hi = hi ^ climate_salt.hi;
    SeedPair a = {xNextLong(&rng), xNextLong(&rng)};
    SeedPair b = {xNextLong(&rng), xNextLong(&rng)};
    return half == 'A' ? a : b;
}

static float centered_offset(uint64_t seed, int active_octave, char half) {
    SeedPair base = half_seed(seed, half), salt = octave_salts[active_octave];
    Xoroshiro rng = {base.lo ^ salt.lo, base.hi ^ salt.hi};
    (void)xNextLong(&rng);
    uint32_t offset24 = (uint32_t)(xNextLong(&rng) >> 32) & 0xffffff;
    return (float)offset24 * (1.0f / 16777216.0f) - 0.5f;
}

int main(void) {
    const uint64_t seed = UINT64_C(2219294028618820316);
    float a0 = centered_offset(seed, 0, 'A');
    float b0 = centered_offset(seed, 0, 'B');
    float a1 = centered_offset(seed, 1, 'A');
    float b1 = centered_offset(seed, 1, 'B');
    float sum = a0 + b0 + a1 + b1;
    printf("seed: %llu\n", (unsigned long long)seed);
    printf("temperature octave 0 A: %.9g\n", a0);
    printf("temperature octave 0 B: %.9g\n", b0);
    printf("temperature octave 1 A: %.9g\n", a1);
    printf("temperature octave 1 B: %.9g\n", b1);
    printf("equal-weight signed temperature crunch sum: %.9g\n", sum);
    return 0;
}
