// File: utils.h
// Project: inc
// Created Date: 17/06/2020
// Author: Shun Suzuki
// -----
// Last Modified: 10/05/2021
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
// For some reason, compiling code that contains such operators will not work. (Even if it doesn't been actually called!)
// Therefore, the following two functions are implemented...
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

static inline uint32_t mod_n_pows_of_two(uint64_t value, uint16_t n) {
  uint64_t rv = value >> n;
  uint64_t rest = (rv - (uint64_t)mod1e9_u64(rv)) << n;
  return (uint32_t)(value - rest);
}

#endif  // INC_UTILS_H_
