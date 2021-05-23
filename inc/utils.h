// File: utils.h
// Project: inc
// Created Date: 17/06/2020
// Author: Shun Suzuki
// -----
// Last Modified: 17/05/2021
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

#endif  // INC_UTILS_H_
