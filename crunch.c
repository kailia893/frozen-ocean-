#include "crunch.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
  uint64_t climate_salt_lo;
  uint64_t climate_salt_hi;
  int octave_min;
  int octave_length;
  int amplitude_length;
  float amplitudes[9];
} CrunchConfig;
typedef struct { uint64_t lo; uint64_t hi; } SeedPair;

static const CrunchConfig crunch_configs[NP_MAX] = {
  [NP_TEMPERATURE] = {0x944b0073edf549dbULL, 0x4ff44347e9d22b96ULL, -12, 3, 6, {1.5f, 0.0f, 1.0f}},
  [NP_HUMIDITY] = {0x71b8ab943dbd5301ULL, 0xbb63ddcf39ff7a2bULL, -10, 2, 6, {1.0f, 1.0f}},
  [NP_CONTINENTALNESS] = {0x9a3f51a113fce8dcULL, 0xee2dbd157e5dcdadULL, -11, 9, 9, {1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f}},
  [NP_EROSION] = {0x8c984b1f8702a951ULL, 0xead7b1f92bae535fULL, -11, 5, 5, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
  [NP_WEIRDNESS] = {0xefc8ef4d36102b34ULL, 0x1beeeb324a0f24eaULL, -7, 3, 6, {1.0f, 2.0f, 1.0f}}
};

static const SeedPair octave_salts[17] = {
  {0xc613bf766619f992ULL, 0x954753f86691b86aULL}, {0x7eee475a921c6cf5ULL, 0xf2bd39426f8da413ULL},
  {0xfc0027cef9683114ULL, 0xb758d3954dcbfdd3ULL}, {0xd1fc8a05be565ecaULL, 0xdc2a3915cbdda25bULL},
  {0xb198de63a8012672ULL, 0x7b84cad43ef7b5a8ULL}, {0x0fd787bfbc403ec3ULL, 0x74a4a31ca21b48b8ULL},
  {0x36d326eed40efeb2ULL, 0x5be9ce18223c636aULL}, {0x082fe255f8be6631ULL, 0x4e96119e22dedc81ULL},
  {0x0ef68ec68504005eULL, 0x48b6bf93a2789640ULL}, {0xf11268128982754fULL, 0x257a1d670430b0aaULL},
  {0xe51c98ce7d1de664ULL, 0x5f9478a733040c45ULL}, {0x6d7b49e7e429850aULL, 0x2e3063c622a24777ULL},
  {0xbd90d5377ba1b762ULL, 0xc07317d419a7548dULL}, {0x53d39c6752dac858ULL, 0xbcd1c5a80ab65b3eULL},
  {0xb4a24d7a84e7677bULL, 0x023ff9668e89b5c4ULL}, {0xdffa22b534c5f608ULL, 0xb9b67517d3665ca9ULL},
  {0xd50708086cef4d7cULL, 0x6e1651ecc7f43309ULL}
};

typedef struct {
  uint64_t seed;
  int type;
  SeedPair halves[2];
  int valid;
  int weight_type;
  int weight_valid;
  int octave_count;
  int amplitude_indices[9];
  float octave_weights[9];
} CrunchBaseCache;
static _Thread_local CrunchBaseCache base_cache;

static const CrunchConfig *config_for_type(int type) { return &crunch_configs[type]; }

static void cache_type_weights(const CrunchConfig *config, int type) {
  if (base_cache.weight_valid && base_cache.weight_type == type) return;
  int amp_len = config->amplitude_length;
  float value_factor = 5.0f / 3.0f * config->octave_length
      / (config->octave_length + 1)
      * (1 << (amp_len - 1)) / ((1 << amp_len) - 1);
  int active_octave = 0;
  for (int amp_index = 0; amp_index < amp_len; amp_index++) {
    float amplitude = config->amplitudes[amp_index];
    if (amplitude != 0.0f) {
      base_cache.amplitude_indices[active_octave] = amp_index;
      base_cache.octave_weights[active_octave] = value_factor * amplitude;
      active_octave++;
    }
    value_factor *= 0.5f;
  }
  base_cache.octave_count = active_octave;
  base_cache.weight_type = type;
  base_cache.weight_valid = 1;
}

static SeedPair half_base_seed(const lbNoise *noise, int type,
    const CrunchConfig *config, char half_octave) {
  if (!base_cache.valid || base_cache.seed != noise->seed || base_cache.type != type) {
    Xoroshiro rng;
    xSetSeed(&rng, noise->seed);
    uint64_t seed_lo = xNextLong(&rng);
    uint64_t seed_hi = xNextLong(&rng);
    rng.lo = seed_lo ^ config->climate_salt_lo;
    rng.hi = seed_hi ^ config->climate_salt_hi;
    for (int i = 0; i < 2; i++) {
      base_cache.halves[i].lo = xNextLong(&rng);
      base_cache.halves[i].hi = xNextLong(&rng);
    }
    base_cache.seed = noise->seed;
    base_cache.type = type;
    base_cache.valid = 1;
  }
  return half_octave == 'A' ? base_cache.halves[0] : base_cache.halves[1];
}

static uint32_t octave_offset24(uint64_t base_lo, uint64_t base_hi, int salt_index) {
  const SeedPair *salt = &octave_salts[salt_index];
  Xoroshiro rng = {base_lo ^ salt->lo, base_hi ^ salt->hi};
  (void)xNextLong(&rng);
  return (uint32_t)(xNextLong(&rng) >> 32) & 0xffffff;
}

float crunchiness(lbNoise *noise, int type, int octave_index, char half_octave) {
  const CrunchConfig *config = config_for_type(type);
  cache_type_weights(config, type);
  int amp_index = base_cache.amplitude_indices[octave_index];
  int salt_index = 16 + config->octave_min + amp_index;
  SeedPair base = half_base_seed(noise, type, config, half_octave);
  uint32_t offset24 = octave_offset24(base.lo, base.hi, salt_index);
  float centered_offset = (float)offset24 * (1.0f / 16777216.0f) - 0.5f;
  return centered_offset;
}
