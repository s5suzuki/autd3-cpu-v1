/*
 * File: app.c
 * Project: app_src
 * Created Date: 29/06/2020
 * Author: Shun Suzuki
 * -----
 * Last Modified: 17/05/2021
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
#define SEQ_BUF_SEGMENT_SIZE (2048)

#define BRAM_CONFIG_SELECT (0)
#define BRAM_MOD_SELECT (1)
#define BRAM_TR_SELECT (2)
#define BRAM_SEQ_SELECT (3)

#define CONFIG_CF_AND_CP (0x00)
#define CONFIG_FPGA_INFO (0x01)
#define CONFIG_SEQ_CYCLE (0x02)
#define CONFIG_SEQ_DIV (0x03)
#define CONFIG_SEQ_BRAM_OFFSET (0x07)
#define CONFIG_WAVELENGTH_UM (0x08)
#define CONFIG_SEQ_SYNC_TIME_BASE (0x09)
#define CONFIG_MOD_CYCLE (0x0D)
#define CONFIG_MOD_DIV (0x0E)
#define CONFIG_MOD_SYNC_TIME_BASE (0x0F)
#define CONFIG_FPGA_VER (255)

#define CP_MOD_INIT (0x0100)
#define CP_SEQ_INIT (0x0200)

#define CMD_OP (0x00)
#define CMD_RD_CPU_V_LSB (0x02)
#define CMD_RD_CPU_V_MSB (0x03)
#define CMD_RD_FPGA_V_LSB (0x04)
#define CMD_RD_FPGA_V_MSB (0x05)
#define CMD_SEQ_MODE (0x06)
#define CMD_INIT_MOD_CLOCK (0x07)
#define CMD_CLEAR (0x09)

extern RX_STR0 _sRx0;
extern RX_STR1 _sRx1;
extern TX_STR _sTx;

static volatile uint8_t _header_id = 0;
static volatile uint8_t _commnad = 0;
static volatile uint8_t _ctrl_flag = 0;
static volatile bool_t _read_fpga_info = false;

static volatile uint8_t _mod_buf[MOD_BUF_SIZE];
static volatile uint16_t _mod_size = 0;
static volatile uint16_t _mod_cycle = 0;

static volatile uint16_t _seq_cycle = 0;
static volatile uint16_t _seq_buf_fpga_write = 0;
static volatile bool_t _seq_buf_read_end = 0;

static volatile uint16_t _ack;

// fire when ethercat packet arrives
extern void recv_ethercat(void);
// fire once after power on
extern void init_app(void);
// fire periodically with 1ms interval
extern void update(void);

typedef enum {
  MOD_BEGIN = 1 << 0,
  MOD_END = 1 << 1,
  READ_FPGA_INFO = 1 << 2,
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

static void write_mod_buf(uint32_t write) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_MOD_SELECT, 0);
  word_cpy_volatile(&base[addr], (volatile uint16_t *)_mod_buf, write >> 1);
}

static void clear(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;

  _seq_cycle = 0;
  _seq_buf_fpga_write = 0;
  _seq_buf_read_end = 0;

  _mod_cycle = 4000;
  memset_volatile(_mod_buf, 0xff, MOD_BUF_SIZE);
  write_mod_buf(MOD_BUF_SIZE);

  addr = get_addr(BRAM_TR_SELECT, 0);
  word_set_volatile(&base[addr], 0x0000, TRANS_NUM);

  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, SILENT);
}

void init_app(void) { clear(); }

static void init_mod_clk() {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  volatile uint64_t sys_time;
  volatile uint64_t next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;

  // wait for sync0 activation
  while (ECATC.DC_SYS_TIME.LONGLONG < next_sync0) {
    wait_ns(1000 * MICRO_SECONDS);
  }

  while (true) {
    sys_time = ECATC.DC_SYS_TIME.LONGLONG;
    if (sys_time > next_sync0) {
      if (sys_time < next_sync0 + 500 * MICRO_SECONDS)
        break;
      else
        next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;
    }
  }
  next_sync0 += (uint64_t)ECATC.DC_SYNC0_CYC_TIME.LONG;

  addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_MOD_SYNC_TIME_BASE);
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  asm volatile("dmb");
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_MOD_INIT | _ctrl_flag);
  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static void init_fpga_seq_clk(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  volatile uint64_t sys_time;
  volatile uint64_t next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;

  while (true) {
    sys_time = ECATC.DC_SYS_TIME.LONGLONG;
    if (sys_time > next_sync0) {
      if (sys_time < next_sync0 + 500 * MICRO_SECONDS)
        break;
      else
        next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;
    }
  }
  next_sync0 += (uint64_t)ECATC.DC_SYNC0_CYC_TIME.LONG;
  addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_SEQ_SYNC_TIME_BASE);
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  asm volatile("dmb");
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_SEQ_INIT | _ctrl_flag);
  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & 0xFF00) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static void cmd_op(RxGlobalHeader *header) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint16_t i;

  if ((header->control_flags & SEQ_MODE) == 0) {
    addr = get_addr(BRAM_TR_SELECT, 0);
    word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
  }

  if ((header->control_flags & MOD_BEGIN) != 0) _mod_size = 0;

  memcpy_volatile(&_mod_buf[_mod_size], header->mod, header->mod_size);
  _mod_size += header->mod_size;
  if ((header->control_flags & MOD_END) != 0) {
    for (i = _mod_size; i < _mod_cycle; i += _mod_size) {
      uint16_t write = (i + _mod_size) > _mod_cycle ? _mod_cycle - i : _mod_size;
      memcpy_volatile(&_mod_buf[i], &_mod_buf[0], write);
    }
    write_mod_buf(_mod_cycle);
  }
}

static void write_foci(Focus *foci, uint16_t write) {
  volatile uint16_t *s = (uint16_t *)FPGA_BASE;
  uint16_t i, addr;

  for (i = 0; i < write; i++) {
    addr = get_addr(BRAM_SEQ_SELECT, 8 * (_seq_buf_fpga_write % SEQ_BUF_SEGMENT_SIZE));
    word_cpy_volatile(&s[addr], (volatile uint16_t *)&foci[i], sizeof(Focus));

    _seq_buf_fpga_write++;
    if ((_seq_buf_fpga_write % SEQ_BUF_SEGMENT_SIZE) == 0)
      bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, _seq_buf_fpga_write / SEQ_BUF_SEGMENT_SIZE);
  }
}

static void recv_foci(RxGlobalHeader *header) {
  uint16_t seq_size = _sRx0.data[0];
  uint16_t seq_div = _sRx0.data[1];
  uint16_t wavelength;

  if ((header->control_flags & SEQ_BEGIN) != 0) {
    _seq_cycle = 0;
    _seq_buf_fpga_write = 0;
    _seq_buf_read_end = false;

    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_DIV, seq_div);
    wavelength = _sRx0.data[2];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_WAVELENGTH_UM, wavelength);
  }

  write_foci((Focus *)&_sRx0.data[4], seq_size);
  _seq_cycle += seq_size;

  if ((header->control_flags & SEQ_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_CYCLE, _seq_cycle);
    _seq_buf_read_end = true;
    _ack = 0;
  }
}

static uint16_t get_cpu_version(void) { return CPU_VERSION; }

static uint16_t get_fpga_version(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_VER); }

static uint16_t read_fpga_info(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_INFO); }

void update(void) {
  _ack = (((uint16_t)_header_id) << 8);
  if (_read_fpga_info) _ack |= read_fpga_info();

  switch (_commnad) {
    case CMD_RD_CPU_V_LSB:
    case CMD_RD_CPU_V_MSB:
    case CMD_RD_FPGA_V_LSB:
    case CMD_RD_FPGA_V_MSB:
      break;
    case CMD_CLEAR:
      _commnad = 0x00;
      clear();
      if (_read_fpga_info) _ack |= read_fpga_info();
      break;
    case CMD_INIT_MOD_CLOCK:
      _commnad = 0x00;
      init_mod_clk();
      if (_read_fpga_info) _ack |= read_fpga_info();
      break;
    default:
      if (_read_fpga_info) _ack |= read_fpga_info();
      break;
  }

  if (_seq_buf_read_end && (_seq_buf_fpga_write == _seq_cycle)) {
    _seq_buf_read_end = false;
    init_fpga_seq_clk();
  }

  _sTx.ack = _ack;
}

void recv_ethercat(void) {
  RxGlobalHeader *header = (RxGlobalHeader *)(_sRx1.data);
  if (header->msg_id != _header_id) {
    _header_id = header->msg_id;
    _ack = ((uint16_t)(header->msg_id)) << 8;
    _read_fpga_info = (header->control_flags & READ_FPGA_INFO) != 0;

    switch (header->command) {
      case CMD_OP:
        cmd_op(header);
        _ctrl_flag = header->control_flags;
        bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);
        if (_read_fpga_info) _ack |= read_fpga_info();
        break;

      case CMD_INIT_MOD_CLOCK:
        _mod_cycle = _sRx0.data[0];
        bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_CYCLE, _mod_cycle);
        bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_DIV, _sRx0.data[1]);
        if (_read_fpga_info) _ack = read_fpga_info();
        break;

      case CMD_RD_CPU_V_LSB:
        _ack |= get_cpu_version() & 0xFF;
        break;

      case CMD_RD_CPU_V_MSB:
        _ack |= (get_cpu_version() >> 8) & 0xFF;
        break;

      case CMD_RD_FPGA_V_LSB:
        _ack |= get_fpga_version() & 0xFF;
        break;

      case CMD_RD_FPGA_V_MSB:
        _ack |= (get_fpga_version() >> 8) & 0xFF;
        break;

      case CMD_SEQ_MODE:
        _ctrl_flag = header->control_flags;
        recv_foci(header);
        if (_read_fpga_info) _ack |= read_fpga_info();
        break;

      default:
        if (_read_fpga_info) _ack = read_fpga_info();
        break;
    }
    _sTx.ack = _ack;
    _commnad = header->command;
  }
}
