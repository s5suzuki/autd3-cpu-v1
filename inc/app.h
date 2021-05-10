// File: app.h
// Project: inc
// Created Date: 04/12/2020
// Author: Shun Suzuki
// -----
// Last Modified: 10/05/2021
// Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
// -----
// Copyright (c) 2020 Hapis Lab. All rights reserved.
//

#ifndef APP_H_
#define APP_H_

#ifndef null
#define null 0
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#ifndef uint8_t
typedef unsigned char uint8_t;
#endif
#ifndef uint16_t
typedef unsigned short uint16_t;
#endif
#ifndef uint32_t
typedef unsigned long uint32_t;
#endif
#ifndef uint64_t
typedef long long unsigned int uint64_t;
#endif
#ifndef int8_t
typedef signed char int8_t;
#endif
#ifndef int16_t
typedef signed short int16_t;
#endif
#ifndef int32_t
typedef signed long int32_t;
#endif
#ifndef int64_t
typedef long long int int64_t;
#endif
#ifndef float32_t
typedef float float32_t;
#endif
#ifndef float64_t
typedef double float64_t;
#endif
#ifndef bool_t
typedef int bool_t;
#endif

#define TRANS_NUM (249)
#define TRANS_NUM_IN_X (18)
#define TRANS_NUM_IN_Y (14)
#define TRANS_SIZE (10.18f)
#define ULTRASOUND_WAVELENGTH (8.5f)
#define ULTRASOUND_SCALE (255.0f / ULTRASOUND_WAVELENGTH)

#define LM_BUF_SEGMENT_SIZE (2048)

#define IS_MISSING_TRANSDUCER(X, Y) (Y == 1 && (X == 1 || X == 2 || X == 16))

#define FPGA_BASE 0x44000000 /* CS1 FPGA address */

static inline void word_cpy(uint16_t *dst, uint16_t *src, uint32_t cnt) {
  while (cnt-- > 0) {
    *dst++ = *src++;
  }
}

static inline void word_cpy_volatile(volatile uint16_t *dst, volatile uint16_t *src, uint32_t cnt) {
  while (cnt-- > 0) {
    *dst++ = *src++;
  }
}

static inline void word_set_volatile(volatile uint16_t *dst, uint16_t v, uint32_t cnt) {
  while (cnt-- > 0) {
    *dst++ = v;
  }
}

static inline void word_set(uint16_t *dst, uint16_t v, uint32_t cnt) {
  while (cnt-- > 0) {
    *dst++ = v;
  }
}

inline static uint16_t get_addr(uint8_t bram_select, uint16_t bram_addr) { return (((uint16_t)bram_select & 0x0003) << 14) | (bram_addr & 0x3FFF); }

static inline void bram_write(uint8_t bram_select, uint16_t bram_addr, uint16_t value) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(bram_select, bram_addr);
  base[addr] = value;
}

static inline uint16_t bram_read(uint8_t bram_select, uint16_t bram_addr) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(bram_select, bram_addr);
  return base[addr];
}

static inline void memcpy_volatile(volatile void *dst, volatile const void *src, uint32_t cnt) {
  volatile uint8_t *dst_uc = dst;
  volatile const uint8_t *src_uc = src;
  while (cnt-- > 0) {
    *dst_uc++ = *src_uc++;
  }
}

static inline void memset_volatile(volatile void *s, char c, uint32_t cnt) {
  volatile char *p = s;
  while (cnt-- > 0) {
    *p++ = c;
  }
}

typedef struct {
  uint16_t reserved;
  uint16_t data[249]; /* Data from PC */
} RX_STR0;

typedef struct {
  uint16_t reserved;
  uint16_t data[64]; /* Header from PC */
} RX_STR1;

typedef struct {
  uint16_t reserved;
  uint16_t ack;
} TX_STR;

#endif /* APP_H_ */
