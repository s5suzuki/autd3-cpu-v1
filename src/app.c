/*
 * File: app.c
 * Project: app_src
 * Created Date: 29/06/2020
 * Author: Shun Suzuki
 * -----
 * Last Modified: 12/05/2021
 * Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
 * -----
 * Copyright (c) 2020 Hapis Lab. All rights reserved.
 *
 */

#include "app.h"

#include "iodefine.h"
#include "utils.h"

#define CPU_VERSION (0x000A)  // v1.0

#define MICRO_SECONDS (1000)

#define MOD_BUF_SIZE (32000)
#define REF_CLK_CYCLE_CNT_BASE (40000)
#define MOD_SMPL_CLK_FREQ_BASE (8000)
#define MOD_CLK_DIV (REF_CLK_CYCLE_CNT_BASE / MOD_SMPL_CLK_FREQ_BASE)

#define BRAM_CONFIG_SELECT (0)
#define BRAM_MOD_SELECT (1)
#define BRAM_NORMAL_OP_SELECT (2)

#define CONFIG_CF_AND_CP (0)
#define CONFIG_FPGA_INFO (1)
#define CONFIG_SEQ_CYCLE (2)
#define CONFIG_SEQ_DIV (3)
#define CONFIG_SEQ_SYNC_SHIFT (4)
#define CONFIG_MOD_IDX_SHIFT (5)
#define CONFIG_REF_CLK_CYC_SHIFT (6)
#define CONFIG_FPGA_VER (255)

#define CP_REF_INIT (0x0100)
#define CP_SEQ_INIT (0x0200)
#define CP_RST (0x8000)

#define CMD_OP (0x00)
#define CMD_RD_CPU_V_LSB (0x02)
#define CMD_RD_CPU_V_MSB (0x03)
#define CMD_RD_FPGA_V_LSB (0x04)
#define CMD_RD_FPGA_V_MSB (0x05)
#define CMD_SEQ_MODE (0x06)
#define CMD_INIT_FPGA_REF_CLOCK (0x07)
#define CMD_CLEAR (0x09)
#define CMD_UPDATE_CTRL_FLAG (0x0A)

extern RX_STR0 _sRx0;
extern RX_STR1 _sRx1;
extern TX_STR _sTx;

static volatile uint8_t _header_id = 0;
static volatile uint8_t _commnad = 0;
static volatile uint8_t _ctrl_flag = 0;

static volatile uint8_t _mod_buf[MOD_BUF_SIZE];
static volatile uint16_t _mod_size = 0;

static volatile uint16_t _seq_cycle = 0;
static volatile uint16_t _seq_buf_fpga_write = 0;

static volatile uint16_t _ref_clk_cyc_shift = 0;
static volatile uint16_t _mod_idx_shift = 1;

// fire when ethercat packet arrives
extern void recv_ethercat(void);
// fire once after power on
extern void init_app(void);
// fire periodically with 1ms interval
extern void update(void);

typedef enum {
  MOD_BEGIN = 1 << 0,
  MOD_END = 1 << 1,
  //
  SILENT = 1 << 3,
  FORCE_FAN = 1 << 4,
  SEQ_MODE = 1 << 5,
  SEQ_BEGIN = 1 << 6,
  SEQ_END = 1 << 7,
} RxGlobalControlFlags;

typedef struct {
  uint8_t msg_id;
  uint8_t control_flags;
  uint8_t command;
  uint8_t mod_size;
  uint8_t mod[124];
} RxGlobalHeader;

static inline uint32_t calc_mod_buf_write() { return ((REF_CLK_CYCLE_CNT_BASE << _ref_clk_cyc_shift) / MOD_CLK_DIV) >> _mod_idx_shift; }

static void write_mod_buf(uint32_t write) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_MOD_SELECT, 0);
  word_cpy_volatile(&base[addr], (volatile uint16_t *)_mod_buf, write >> 1);
}

static void clear(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;

  _ref_clk_cyc_shift = 0;

  _mod_idx_shift = 1;
  bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_IDX_SHIFT, _mod_idx_shift);

  memset_volatile(_mod_buf, 0xff, MOD_BUF_SIZE);
  write_mod_buf(MOD_BUF_SIZE);

  addr = get_addr(BRAM_NORMAL_OP_SELECT, 0);
  word_set_volatile(&base[addr], 0x0000, TRANS_NUM);

  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_RST | SILENT);
  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

void init_app(void) { clear(); }

static void init_fpga_ref_clk(void) {
  volatile uint32_t sys_time;
  volatile uint64_t next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;

  bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_IDX_SHIFT, _mod_idx_shift);
  bram_write(BRAM_CONFIG_SELECT, CONFIG_REF_CLK_CYC_SHIFT, _ref_clk_cyc_shift);
  asm volatile("dmb");

  // wait for sync0 activation
  while (ECATC.DC_SYS_TIME.LONGLONG < next_sync0) {
    wait_ns(1000 * MICRO_SECONDS);
  }

  sys_time = mod_n_pows_of_two(ECATC.DC_SYS_TIME.LONGLONG + 1000 * MICRO_SECONDS, _ref_clk_cyc_shift);
  while (sys_time > 50 * MICRO_SECONDS) {
    sys_time = mod_n_pows_of_two(ECATC.DC_SYS_TIME.LONGLONG + 1000 * MICRO_SECONDS, _ref_clk_cyc_shift);
  }
  wait_ns(50 * MICRO_SECONDS);
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_REF_INIT | _ctrl_flag);

  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static void cmd_op(RxGlobalHeader *header) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint32_t i;
  uint32_t mod_write;

  if ((header->control_flags & SEQ_MODE) == 0) {
    _seq_cycle = 0;
    _seq_buf_fpga_write = 0;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_DIV, 0xFFFF);

    addr = get_addr(BRAM_NORMAL_OP_SELECT, 0);
    word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
  }

  if ((header->control_flags & MOD_BEGIN) != 0) {
    _mod_size = 0;
  }
  memcpy_volatile(&_mod_buf[_mod_size], header->mod, header->mod_size);
  _mod_size += header->mod_size;
  if ((header->control_flags & MOD_END) != 0) {
    mod_write = calc_mod_buf_write();
    for (i = _mod_size; i < mod_write; i += _mod_size) {
      uint16_t write = (i + _mod_size) > mod_write ? mod_write - i : _mod_size;
      memcpy_volatile(&_mod_buf[i], &_mod_buf[0], write);
    }
    write_mod_buf(mod_write);
  }
}

static uint16_t get_cpu_version(void) { return CPU_VERSION; }

static uint16_t get_fpga_version(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_VER); }

void update(void) {
  switch (_commnad) {
    case 0x00:
      break;
    case CMD_CLEAR:
      _commnad = 0x00;
      clear();
      _sTx.ack = ((uint16_t)_header_id) << 8;
      break;
    case CMD_INIT_FPGA_REF_CLOCK:
      _commnad = 0x00;
      init_fpga_ref_clk();
      _sTx.ack = ((uint16_t)_header_id) << 8;
      break;
  }
}

void recv_ethercat(void) {
  RxGlobalHeader *header = (RxGlobalHeader *)(_sRx1.data);
  if (header->msg_id != _header_id) {
    _header_id = header->msg_id;

    switch (header->command) {
      case CMD_OP:
        _commnad = 0x00;
        cmd_op(header);
        _ctrl_flag = header->control_flags;
        bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      case CMD_INIT_FPGA_REF_CLOCK:
        _mod_idx_shift = _sRx0.data[0];
        _ref_clk_cyc_shift = _sRx0.data[1];
        _commnad = header->command;
        break;

      case CMD_RD_CPU_V_LSB:
        _sTx.ack = (((uint16_t)(header->msg_id)) << 8) | (get_cpu_version() & 0x00FF);
        break;

      case CMD_RD_CPU_V_MSB:
        _sTx.ack = (((uint16_t)(header->msg_id)) << 8) | ((get_cpu_version() & 0xFF00) >> 8);
        break;

      case CMD_RD_FPGA_V_LSB:
        _sTx.ack = (((uint16_t)(header->msg_id)) << 8) | (get_fpga_version() & 0x00FF);
        break;

      case CMD_RD_FPGA_V_MSB:
        _sTx.ack = (((uint16_t)(header->msg_id)) << 8) | ((get_fpga_version() & 0xFF00) >> 8);
        break;

      case CMD_UPDATE_CTRL_FLAG:
        _ctrl_flag = header->control_flags;
        bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      default:
        _commnad = header->command;
        break;
    }
  }
}
