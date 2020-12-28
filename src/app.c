/*
 * File: app.c
 * Project: app_src
 * Created Date: 29/06/2020
 * Author: Shun Suzuki
 * -----
 * Last Modified: 24/12/2020
 * Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
 * -----
 * Copyright (c) 2020 Hapis Lab. All rights reserved.
 *
 */

#include "app.h"

#include "iodefine.h"
#include "utils.h"

#define CPU_VERSION (0x0005)  // v0.8

#define MICRO_SECONDS (1000)

#define MOD_BUF_SIZE (32000)
#define REF_CLK_CYCLE_CNT_BASE (40000)

#define BRAM_PROPS_SELECT (0)
#define BRAM_MOD_SELECT (1)
#define BRAM_NORMAL_OP_SELECT (2)
#define BRAM_LM_SELECT (3)

#define CF_AND_CP_ADDR (0)
#define LM_CYC_ADDR (1)
#define LM_DIV_ADDR (2)
#define LM_INIT_LAP_ADDR (3)
#define LM_CALIB_SHIFT_ADDR (4)
#define LM_BRAM_ADDR_OFFSET_ADDR (5)
#define MOD_IDX_SHIFT_ADDR (6)
#define REF_CLK_CYC_SHIFT_ADDR (7)
#define FPGA_VER_ADDR (255)

#define PROPS_REF_INIT (0x0100)
#define PROPS_LM_INIT (0x0200)
#define PROPS_LM_CALIB (0x0400)
#define PROPS_RST (0x8000)

#define CMD_OP (0x00)
#define CMD_WR_BRAM (0x01)
#define CMD_RD_CPU_V_LSB (0x02)
#define CMD_RD_CPU_V_MSB (0x03)
#define CMD_RD_FPGA_V_LSB (0x04)
#define CMD_RD_FPGA_V_MSB (0x05)
#define CMD_LM_MODE (0x06)
#define CMD_INIT_FPGA_REF_CLOCK (0x07)
#define CMD_CALIB_FPGA_LM_CLOCK (0x08)
#define CMD_CLEAR (0x09)
#define CMD_SET_DELAY (0x0A)

extern RX_STR0 _sRx0;
extern RX_STR1 _sRx1;
extern TX_STR _sTx;

static volatile uint8_t _header_id = 0;
static volatile uint8_t _commnad = 0;
static volatile uint8_t _ctrl_flag = 0;
static uint16_t _shift = 0;

static volatile uint8_t _mod_buf[MOD_BUF_SIZE];
static volatile uint16_t _mod_size = 0;

static volatile uint16_t _lm_cycle = 0;
static volatile bool_t _lm_end = false;
static volatile uint16_t _lm_buf_fpga_write = 0;

static volatile uint16_t _ref_clk_cyc_shift = 0;
static volatile uint16_t _mod_idx_shift = 1;

// fire when ethercat packet arrives
extern void recv_ethercat(void);
// fire once after power on
extern void init_app(void);
// fire periodically with 1ms interval
extern void update(void);

typedef enum {
  LOOP_BEGIN = 1 << 0,
  LOOP_END = 1 << 1,
  //
  SILENT = 1 << 3,
  FORCE_FAN = 1 << 4,
  LM_MODE = 1 << 5,
  LM_BEGIN = 1 << 6,
  LM_END = 1 << 7,
} RxGlobalControlFlags;

typedef struct {
  uint8_t msg_id;
  uint8_t control_flags;
  uint8_t command;
  uint8_t mod_size;
  uint16_t lm_size;
  uint16_t lm_div;
  uint8_t mod[120];
} RxGlobalHeader;

static inline uint32_t calc_mod_buf_write() { return ((REF_CLK_CYCLE_CNT_BASE << _ref_clk_cyc_shift) / 5) >> _mod_idx_shift; }

static void write_mod_buf(uint32_t write) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_MOD_SELECT, 0);
  word_cpy_volatile(&base[addr], (volatile uint16_t *)_mod_buf, write >> 1);
}

static void write_foci(Focus *foci, uint16_t write) {
  volatile uint16_t *s = (uint16_t *)FPGA_BASE;
  uint16_t i, addr;

  for (i = 0; i < write; i++) {
    addr = get_addr(BRAM_LM_SELECT, 8 * (_lm_buf_fpga_write % LM_BUF_SEGMENT_SIZE));
    s[addr] = foci[i].x15_0;
    s[addr + 1] = foci[i].y7_0_x23_16;
    s[addr + 2] = foci[i].y23_8;
    s[addr + 3] = foci[i].z15_0;
    s[addr + 4] = foci[i].amp_z23_16;

    _lm_buf_fpga_write++;
    if ((_lm_buf_fpga_write % LM_BUF_SEGMENT_SIZE) == 0) {
      bram_write(BRAM_PROPS_SELECT, LM_BRAM_ADDR_OFFSET_ADDR, _lm_buf_fpga_write / LM_BUF_SEGMENT_SIZE);
    }
  }
}

static void clear(void) {
  _ref_clk_cyc_shift = 0;

  _mod_idx_shift = 1;
  bram_write(BRAM_PROPS_SELECT, MOD_IDX_SHIFT_ADDR, _mod_idx_shift);

  memset_volatile(_mod_buf, 0xff, MOD_BUF_SIZE);
  write_mod_buf(MOD_BUF_SIZE);

  bram_write(BRAM_PROPS_SELECT, LM_DIV_ADDR, 0xFFFF);

  bram_write(BRAM_PROPS_SELECT, CF_AND_CP_ADDR, PROPS_RST | SILENT);
  asm volatile("dmb");
  while ((bram_read(BRAM_PROPS_SELECT, CF_AND_CP_ADDR) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

void init_app(void) { clear(); }

static void init_fpga_ref_clk(void) {
  volatile uint32_t sys_time;
  volatile uint64_t next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;

  bram_write(BRAM_PROPS_SELECT, MOD_IDX_SHIFT_ADDR, _mod_idx_shift);
  bram_write(BRAM_PROPS_SELECT, REF_CLK_CYC_SHIFT_ADDR, _ref_clk_cyc_shift);
  asm volatile("dmb");

  // wait for sync0 activation
  while (ECATC.DC_SYS_TIME.LONGLONG < next_sync0) {
    wait_ns(1000 * MICRO_SECONDS);
  }

  sys_time = mod_n_pows_of_two_e9_u64(ECATC.DC_SYS_TIME.LONGLONG + 1000 * MICRO_SECONDS, _ref_clk_cyc_shift);
  while (sys_time > 50 * MICRO_SECONDS) {
    sys_time = mod_n_pows_of_two_e9_u64(ECATC.DC_SYS_TIME.LONGLONG + 1000 * MICRO_SECONDS, _ref_clk_cyc_shift);
  }
  wait_ns(50 * MICRO_SECONDS);
  bram_write(BRAM_PROPS_SELECT, CF_AND_CP_ADDR, PROPS_REF_INIT | _ctrl_flag);

  asm volatile("dmb");
  while ((bram_read(BRAM_PROPS_SELECT, CF_AND_CP_ADDR) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static uint16_t init_fpga_stm_clk(void) {
  volatile uint16_t lap;

  bram_write(BRAM_PROPS_SELECT, CF_AND_CP_ADDR, PROPS_LM_INIT | _ctrl_flag);

  asm volatile("dmb");
  lap = bram_read(BRAM_PROPS_SELECT, LM_INIT_LAP_ADDR);
  while ((lap & 0x0400) != 0x0400) {
    wait_ns(50 * MICRO_SECONDS);
    lap = bram_read(BRAM_PROPS_SELECT, LM_INIT_LAP_ADDR);
  }

  while ((bram_read(BRAM_PROPS_SELECT, CF_AND_CP_ADDR) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);

  bram_write(BRAM_PROPS_SELECT, LM_INIT_LAP_ADDR, 0x0000);

  return lap & 0x03FF;
}

static void calib_fpga_stm_clk(void) {
  if (_shift != 0) {
    bram_write(BRAM_PROPS_SELECT, CF_AND_CP_ADDR, PROPS_LM_CALIB | _ctrl_flag);
    asm volatile("dmb");
    while ((bram_read(BRAM_PROPS_SELECT, CF_AND_CP_ADDR) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
    bram_write(BRAM_PROPS_SELECT, LM_CALIB_SHIFT_ADDR, 0x0000);
  }
}

static void cmd_op(RxGlobalHeader *header) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint32_t i;
  uint32_t mod_write;

  if ((header->control_flags & LM_MODE) == 0) {
    _lm_cycle = 0;
    _lm_buf_fpga_write = 0;
    bram_write(BRAM_PROPS_SELECT, LM_DIV_ADDR, 0xFFFF);

    addr = get_addr(BRAM_NORMAL_OP_SELECT, 0);
    word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
  }

  if ((header->control_flags & LOOP_BEGIN) != 0) {
    _mod_size = 0;
  }
  memcpy_volatile(&_mod_buf[_mod_size], header->mod, header->mod_size);
  _mod_size += header->mod_size;
  if ((header->control_flags & LOOP_END) != 0) {
    mod_write = calc_mod_buf_write();
    for (i = _mod_size; i < mod_write; i += _mod_size) {
      uint16_t write = (i + _mod_size) > mod_write ? mod_write - i : _mod_size;
      memcpy_volatile(&_mod_buf[i], &_mod_buf[0], write);
    }
    write_mod_buf(mod_write);
  }
}

static void cmd_set_delay(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_NORMAL_OP_SELECT, 256);

  word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
}

static void cmd_wr_bram(void) {
  volatile uint16_t *s = (volatile uint16_t *)FPGA_BASE;
  uint32_t i;
  uint16_t addr, d;
  uint32_t *data = (uint32_t *)_sRx0.data;
  uint32_t len = data[0];
  for (i = 0; i < len; i++) {
    addr = (uint16_t)((data[i + 1] & 0xFFFF0000) >> 16);
    d = (uint16_t)(data[i + 1] & 0x0000FFFF);
    s[addr] = d;
  }
}

static uint16_t get_cpu_version(void) { return CPU_VERSION; }

static uint16_t get_fpga_version(void) { return bram_read(BRAM_PROPS_SELECT, FPGA_VER_ADDR); }

void update(void) {
  uint16_t r;
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
    case CMD_CALIB_FPGA_LM_CLOCK:
      _commnad = 0x00;
      calib_fpga_stm_clk();
      _sTx.ack = 0xE000;
      break;
  }

  if (_lm_end && (_lm_buf_fpga_write == _lm_cycle)) {
    _lm_end = false;
    r = init_fpga_stm_clk();
    _sTx.ack = 0xC000 | r;
  }
}

static void recv_foci(RxGlobalHeader *header) {
  if ((header->control_flags & LM_BEGIN) != 0) {
    _lm_cycle = 0;
    _lm_buf_fpga_write = 0;
    _lm_end = false;
  }

  write_foci((Focus *)_sRx0.data, header->lm_size);
  _lm_cycle += header->lm_size;

  if ((header->control_flags & LM_END) != 0) {
    bram_write(BRAM_PROPS_SELECT, LM_BRAM_ADDR_OFFSET_ADDR, 0);
    bram_write(BRAM_PROPS_SELECT, LM_DIV_ADDR, header->lm_div);
    bram_write(BRAM_PROPS_SELECT, LM_CYC_ADDR, header->lm_div * _lm_cycle);
    asm volatile("dmb");
    _lm_end = true;
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
        bram_write(BRAM_PROPS_SELECT, CF_AND_CP_ADDR, _ctrl_flag);
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      case CMD_WR_BRAM:
        cmd_wr_bram();
        _ctrl_flag = header->control_flags;
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      case CMD_LM_MODE:
        recv_foci(header);
        _ctrl_flag = header->control_flags;
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      case CMD_INIT_FPGA_REF_CLOCK:
        _mod_idx_shift = _sRx0.data[0];
        _ref_clk_cyc_shift = _sRx0.data[1];
        _commnad = header->command;
        break;

      case CMD_CALIB_FPGA_LM_CLOCK:
        _shift = _sRx0.data[0];
        if (_shift == 0) {
          _sTx.ack = 0xE000;
        } else {
          bram_write(BRAM_PROPS_SELECT, LM_CALIB_SHIFT_ADDR, _shift);
        }
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

      case CMD_SET_DELAY:
        _commnad = 0x00;
        cmd_set_delay();
        _sTx.ack = ((uint16_t)(header->msg_id)) << 8;
        break;

      default:
        _commnad = header->command;
        break;
    }
  }
}
