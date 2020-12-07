// File: utils.h
// Project: inc
// Created Date: 17/06/2020
// Author: Shun Suzuki
// -----
// Last Modified: 07/12/2020
// Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
// -----
// Copyright (c) 2020 Hapis Lab. All rights reserved.
//

#ifndef INC_UTILS_H_
#define INC_UTILS_H_

#define CPU_CLK (300)
#define WAIT_LOOP_CYCLE (5)

__attribute__((noinline)) static void wait_ns(uint32_t value) {
  uint32_t wait = (value * 10) / (10000 / CPU_CLK) / WAIT_LOOP_CYCLE + 1;

  __asm volatile(
      "mov   r0,%0     \n"
      "eth_wait_loop:  \n"
      "nop             \n"
      "nop             \n"
      "nop             \n"
      "subs  r0,r0,#1  \n"
      "bne   eth_wait_loop"
      :
      : "r"(wait));
}

// Can make these more elegant and effective?
//
// Division operator (/) and modulo operator (%) are not available when compiling with Release configuration.
// For some reason, compiling code that contains such operators will not work. (Even if I don't actually call it!)
// Therefore, I implemented following two functions...
static inline uint32_t mod1e9_u32(uint32_t value) {
  if (value < 1000000000UL)
    return value;
  else if (value < 2000000000UL)
    return value - 1000000000UL;
  else if (value < 3000000000UL)
    return value - 2000000000UL;
  else if (value < 4000000000UL)
    return value - 3000000000UL;
  else
    return value - 4000000000UL;
}

static inline uint32_t mod1e9_u64(uint64_t value) {
  int i;
  uint32_t msw = (uint32_t)((value & 0xFFFFFFFF00000000) >> 32);
  uint32_t lsw = (uint32_t)((value & 0x00000000FFFFFFFF));

  uint32_t tmp = mod1e9_u32(msw);
  for (i = 0; i < 16; i++) tmp = mod1e9_u32(tmp << 2);
  tmp += mod1e9_u32(lsw);

  return mod1e9_u32(tmp);
}

static inline uint32_t mod_n_pows_of_two_e9_u64(uint64_t value, uint16_t n) {
  uint64_t rv = value >> n;
  uint64_t rest = (rv - (uint64_t)mod1e9_u64(rv)) << n;
  return (uint32_t)(value - rest);
}

inline static uint16_t min(uint16_t l, uint16_t r) { return l < r ? l : r; }

// By fast inverse square root
// See https://en.wikipedia.org/wiki/Fast_inverse_square_root
inline static float32_t sqrt_f(float32_t x) {
  float32_t x2 = 0.5f * x;
  int32_t initial = 0x5F3759DF - (*(int32_t *)&x >> 1);
  float32_t y = *(float32_t *)&initial;

  y *= (1.5f - (x2 * y * y));
  return y * x;
}

inline static float32_t mod_f(float32_t x, float32_t y) {
  int32_t n = (int32_t)(x / y);
  return x - n * y;
}

#endif  // INC_UTILS_H_
