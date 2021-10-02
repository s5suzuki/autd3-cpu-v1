/*
 * File: app.c
 * Project: app_src
 * Created Date: 29/06/2020
 * Author: Shun Suzuki
 * -----
 * Last Modified: 30/09/2021
 * Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
 * -----
 * Copyright (c) 2020 Hapis Lab. All rights reserved.
 *
 */

#include "app.h"

#include "iodefine.h"
#include "utils.h"

#define CPU_VERSION (0x0012) /* v1.8 */

#define MICRO_SECONDS (1000)

#define SEQ_BUF_FOCI_SEGMENT_SIZE (0xFFF)
#define SEQ_BUF_GAIN_SEGMENT_SIZE (0x3F)
#define MOD_BUF_SEGMENT_SIZE (0x7FFF)

#define BRAM_CONFIG_SELECT (0)
#define BRAM_MOD_SELECT (1)
#define BRAM_TR_SELECT (2)
#define BRAM_SEQ_SELECT (3)

#define CONFIG_CF_AND_CP (0x00)
#define CONFIG_FPGA_INFO (0x01)
#define CONFIG_SEQ_CYCLE (0x02)
#define CONFIG_SEQ_DIV (0x03)
#define CONFIG_MOD_BRAM_OFFSET (0x06)
#define CONFIG_SEQ_BRAM_OFFSET (0x07)
#define CONFIG_WAVELENGTH_UM (0x08)
#define CONFIG_SEQ_SYNC_TIME_BASE (0x09)
#define CONFIG_MOD_CYCLE (0x0D)
#define CONFIG_MOD_DIV (0x0E)
#define CONFIG_MOD_SYNC_TIME_BASE (0x0F)
#define CONFIG_FPGA_VER (0x3F)

#define TR_DELAY_OFFSET_BASE_ADDR (0x100)

#define CP_MOD_INIT (0x4000)
#define CP_SEQ_INIT (0x8000)

#define OP_MODE_NORMAL (0)
#define OP_MODE_SEQ (1)

#define CMD_OP (0x00)
#define CMD_RD_CPU_V_LSB (0x02)
#define CMD_RD_CPU_V_MSB (0x03)
#define CMD_RD_FPGA_V_LSB (0x04)
#define CMD_RD_FPGA_V_MSB (0x05)
#define CMD_SEQ_FOCI_MODE (0x06)
#define CMD_CLEAR (0x09)
#define CMD_SET_DELAY_OFFSET (0x0A)
#define CMD_SEQ_GAIN_MODE (0x0D)

#define GAIN_DATA_MODE_PHASE_DUTY_FULL (0x0001)
#define GAIN_DATA_MODE_PHASE_FULL (0x0002)
#define GAIN_DATA_MODE_PHASE_HALF (0x0004)

extern RX_STR0 _sRx0;
extern RX_STR1 _sRx1;
extern TX_STR _sTx;

static volatile uint8_t _header_id = 0;
static volatile uint8_t _commnad = 0;
static volatile uint16_t _ctrl_flag = 0;
static volatile bool_t _read_fpga_info = false;

static volatile uint32_t _mod_cycle = 0;
static volatile uint32_t _mod_buf_fpga_write = 0;
static volatile bool_t _mod_buf_write_end = 0;

static volatile uint32_t _seq_cycle = 0;
static volatile uint32_t _seq_buf_fpga_write = 0;
static volatile bool_t _seq_buf_write_end = 0;
static volatile uint16_t _seq_gain_data_mode = GAIN_DATA_MODE_PHASE_DUTY_FULL;
static volatile uint16_t _seq_gain_size;

static volatile uint16_t _ack;

// fire when ethercat packet arrives
extern void recv_ethercat(void);
// fire once after power on
extern void init_app(void);
// fire periodically with 1ms interval
extern void update(void);

typedef enum {
  OUTPUT_ENABLE = 1 << 0,
  OUTPUT_BALANCE = 1 << 1,
  SILENT = 1 << 3,
  FORCE_FAN = 1 << 4,
  OP_MODE = 1 << 5,
  SEQ_MODE = 1 << 6,
} FPGAControlFlags;

typedef enum {
  MOD_BEGIN = 1 << 0,
  MOD_END = 1 << 1,
  SEQ_BEGIN = 1 << 2,
  SEQ_END = 1 << 3,
  READS_FPGA_INFO = 1 << 4,
} CPUControlFlags;

typedef struct {
  uint8_t msg_id;
  uint8_t control_flags;
  uint8_t command;
  uint8_t command_flags;
  uint8_t mod_size;
  uint8_t mod[123];
} GlobalHeader;

inline static uint64_t wait_sync0() {
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
  return next_sync0 + (uint64_t)ECATC.DC_SYNC0_CYC_TIME.LONG;
}

static void clear(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;

  _ctrl_flag = SILENT;
  _read_fpga_info = false;
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);

  _seq_cycle = 0;
  _seq_buf_fpga_write = 0;
  _seq_buf_write_end = false;

  _mod_cycle = 4000;
  _mod_buf_fpga_write = 0;
  _mod_buf_write_end = false;
  bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_CYCLE, max(1, _mod_cycle) - 1);
  bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_DIV, 10);
  addr = get_addr(BRAM_MOD_SELECT, 0);
  word_set_volatile(&base[addr], 0xFFFF, _mod_cycle >> 1);

  addr = get_addr(BRAM_TR_SELECT, 0);
  word_set_volatile(&base[addr], 0x0000, TRANS_NUM);

  addr = get_addr(BRAM_TR_SELECT, TR_DELAY_OFFSET_BASE_ADDR);
  word_set_volatile(&base[addr], 0xFF00, TRANS_NUM);
}

void init_app(void) { clear(); }

static void write_mod(volatile uint16_t *mod, uint8_t write) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint8_t i;

  for (i = 0; i < (write >> 1); i++) {
    addr = get_addr(BRAM_MOD_SELECT, (_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) >> 1);
    word_cpy_volatile(&base[addr], &mod[i], 1);
    _mod_buf_fpga_write += 2;
    if ((_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) == 0) bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_BRAM_OFFSET, _mod_buf_fpga_write >> 15);
  }

  if ((write & 0x1) != 0) {
    addr = get_addr(BRAM_MOD_SELECT, (_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) >> 1);
    word_cpy_volatile(&base[addr], &mod[i], 1);
    _mod_buf_fpga_write += 1;
  }
}

static void init_mod_clk() {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_MOD_SYNC_TIME_BASE);
  uint64_t next_sync0 = wait_sync0();
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  asm volatile("dmb");
  wait_ns(50 * MICRO_SECONDS);
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_MOD_INIT | _ctrl_flag);
  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & CP_MOD_INIT) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static void write_foci(volatile Focus *foci, uint16_t write) {
  volatile uint16_t *base = (uint16_t *)FPGA_BASE;
  uint16_t i, addr;
  for (i = 0; i < write; i++) {
    // 2bit left shift = *4=sizeof(Focus)/16
    addr = get_addr(BRAM_SEQ_SELECT, (_seq_buf_fpga_write & SEQ_BUF_FOCI_SEGMENT_SIZE) << 2);
    word_cpy_volatile(&base[addr], (volatile uint16_t *)&foci[i], sizeof(Focus) >> 1);
    _seq_buf_fpga_write++;
    // 12bit right shift = /SEQ_BUF_FOCI_SEGMENT_SIZE
    if ((_seq_buf_fpga_write & SEQ_BUF_FOCI_SEGMENT_SIZE) == 0) bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, _seq_buf_fpga_write >> 12);
  }
}

void recv_foci(GlobalHeader *header) {
  uint16_t seq_div;
  uint16_t wavelength;
  uint16_t seq_size = _sRx0.data[0];
  uint32_t offset = 1;

  if ((header->command_flags & SEQ_BEGIN) != 0) {
    _seq_cycle = 0;
    _seq_buf_fpga_write = 0;
    _seq_buf_write_end = false;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, 0);
    seq_div = _sRx0.data[1];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_DIV, seq_div);
    wavelength = _sRx0.data[2];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_WAVELENGTH_UM, wavelength);
    offset += 4;
  }

  write_foci((volatile Focus *)&_sRx0.data[offset], seq_size);
  _seq_cycle += seq_size;

  if ((header->command_flags & SEQ_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_CYCLE, max(1, _seq_cycle) - 1);
    _seq_buf_write_end = true;
    _ack = 0;
  }
}

void recv_gain_seq(GlobalHeader *header) {
  volatile uint16_t *base = (uint16_t *)FPGA_BASE;
  uint16_t seq_div;
  uint16_t addr;
  uint8_t i;
  uint16_t duty = 0xFF00;
  uint16_t phase;

  if ((header->command_flags & SEQ_BEGIN) != 0) {
    _seq_cycle = 0;
    _seq_buf_fpga_write = 0;
    _seq_buf_write_end = false;
    _seq_gain_data_mode = _sRx0.data[0];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, 0);
    seq_div = _sRx0.data[1];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_DIV, seq_div);
    _seq_gain_size = _sRx0.data[2];
    return;
  }

  // sizeof(SeqFocus) is 64 bit, thus, the memory address of the Gain data in Sequence is aligned to 64
  // the number of transducers is 249, so the size of a Gain in Sequence is 16*256
  addr = get_addr(BRAM_SEQ_SELECT, (_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) << 8);

  switch (_seq_gain_data_mode) {
    case GAIN_DATA_MODE_PHASE_DUTY_FULL:
      word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
      _seq_cycle++;
      break;
    case GAIN_DATA_MODE_PHASE_FULL:
      for (i = 0; i < TRANS_NUM; i++) base[addr + i] = duty | (_sRx0.data[i] & 0x00FF);
      _seq_cycle++;
      addr = get_addr(BRAM_SEQ_SELECT, (_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) << 8);
      for (i = 0; i < TRANS_NUM; i++) base[addr + i] = duty | ((_sRx0.data[i] >> 8) & 0x00FF);
      _seq_cycle++;
      break;
    case GAIN_DATA_MODE_PHASE_HALF:
      for (i = 0; i < TRANS_NUM; i++) {
        phase = _sRx0.data[i] & 0x000F;
        phase = (phase << 4) + phase;
        base[addr + i] = duty | phase;
      }
      _seq_cycle++;
      addr = get_addr(BRAM_SEQ_SELECT, (_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) << 8);
      for (i = 0; i < TRANS_NUM; i++) {
        phase = (_sRx0.data[i] >> 4) & 0x000F;
        phase = (phase << 4) + phase;
        base[addr + i] = duty | phase;
      }
      _seq_cycle++;
      addr = get_addr(BRAM_SEQ_SELECT, (_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) << 8);
      for (i = 0; i < TRANS_NUM; i++) {
        phase = (_sRx0.data[i] >> 8) & 0x000F;
        phase = (phase << 4) + phase;
        base[addr + i] = duty | phase;
      }
      _seq_cycle++;
      addr = get_addr(BRAM_SEQ_SELECT, (_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) << 8);
      for (i = 0; i < TRANS_NUM; i++) {
        phase = (_sRx0.data[i] >> 12) & 0x000F;
        phase = (phase << 4) + phase;
        base[addr + i] = duty | phase;
      }
      _seq_cycle++;
      break;
    default:
      word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
      _seq_cycle++;
      break;
  }

  // 6bit right shift = /SEQ_BUF_GAIN_SEGMENT_SIZE
  if ((_seq_cycle & SEQ_BUF_GAIN_SEGMENT_SIZE) == 0) bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, _seq_cycle >> 6);

  if ((header->command_flags & SEQ_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_CYCLE, max(1, _seq_gain_size) - 1);
    _seq_buf_write_end = true;
    _ack = 0;
  }
}

static void init_fpga_seq_clk(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_SEQ_SYNC_TIME_BASE);
  uint64_t next_sync0 = wait_sync0();
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  asm volatile("dmb");
  wait_ns(50 * MICRO_SECONDS);
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, CP_SEQ_INIT | _ctrl_flag);
  asm volatile("dmb");
  while ((bram_read(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP) & CP_SEQ_INIT) != 0x0000) wait_ns(50 * MICRO_SECONDS);
}

static void cmd_op(GlobalHeader *header) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint16_t mod_div;
  uint32_t offset = 0;

  if ((header->control_flags & OP_MODE) == OP_MODE_NORMAL) {
    addr = get_addr(BRAM_TR_SELECT, 0);
    word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
  }

  if ((header->command_flags & MOD_BEGIN) != 0) {
    _mod_cycle = 0;
    _mod_buf_fpga_write = 0;
    _mod_buf_write_end = false;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_BRAM_OFFSET, 0);
    mod_div = (((uint16_t)header->mod[1] << 8) & 0xFF00) | (header->mod[0] & 0x00FF);
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_DIV, mod_div);
    offset += 2;
  }
  write_mod((volatile uint16_t *)&header->mod[offset], header->mod_size);
  _mod_cycle += header->mod_size;

  if ((header->command_flags & MOD_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_CYCLE, max(1, _mod_cycle) - 1);
    _mod_buf_write_end = true;
    _ack = 0;
  }
}

static void cmd_set_delay_offset(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_TR_SELECT, TR_DELAY_OFFSET_BASE_ADDR);
  word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
}

static uint16_t get_cpu_version(void) { return CPU_VERSION; }
static uint16_t get_fpga_version(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_VER); }
static uint16_t read_fpga_info(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_INFO); }

void update(void) {
  switch (_commnad) {
    case CMD_RD_CPU_V_LSB:
    case CMD_RD_CPU_V_MSB:
    case CMD_RD_FPGA_V_LSB:
    case CMD_RD_FPGA_V_MSB:
      break;
    default:
      if (_read_fpga_info) _ack = (_ack & 0xFF00) | read_fpga_info();
      break;
  }

  if (_mod_buf_write_end) {
    _mod_buf_write_end = false;
    init_mod_clk();
    _ack = ((uint16_t)_header_id) << 8;
    if (_read_fpga_info) _ack |= read_fpga_info();
  }

  if (_seq_buf_write_end) {
    _seq_buf_write_end = false;
    init_fpga_seq_clk();
    _ctrl_flag |= OUTPUT_ENABLE;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);
    _ack = ((uint16_t)_header_id) << 8;
    if (_read_fpga_info) _ack |= read_fpga_info();
  }

  _sTx.ack = _ack;
}

void recv_ethercat(void) {
  GlobalHeader *header = (GlobalHeader *)(_sRx1.data);
  if (header->msg_id == _header_id) return;
  _header_id = header->msg_id;
  _ack = ((uint16_t)(header->msg_id)) << 8;
  _read_fpga_info = (header->command_flags & READS_FPGA_INFO) != 0;
  if (_read_fpga_info) _ack |= read_fpga_info();

  switch (header->command) {
    case CMD_OP:
      cmd_op(header);
      _ctrl_flag = header->control_flags;
      bram_write(BRAM_CONFIG_SELECT, CONFIG_CF_AND_CP, _ctrl_flag);
      break;

    case CMD_CLEAR:
      clear();
      break;

    case CMD_RD_CPU_V_LSB:
      _ack = (_ack & 0xFF00) | (get_cpu_version() & 0xFF);
      break;

    case CMD_RD_CPU_V_MSB:
      _ack = (_ack & 0xFF00) | ((get_cpu_version() >> 8) & 0xFF);
      break;

    case CMD_RD_FPGA_V_LSB:
      _ack = (_ack & 0xFF00) | (get_fpga_version() & 0xFF);
      break;

    case CMD_RD_FPGA_V_MSB:
      _ack = (_ack & 0xFF00) | ((get_fpga_version() >> 8) & 0xFF);
      break;

    case CMD_SEQ_FOCI_MODE:
      _ctrl_flag = header->control_flags;
      recv_foci(header);
      break;

    case CMD_SEQ_GAIN_MODE:
      _ctrl_flag = header->control_flags;
      recv_gain_seq(header);
      break;

    case CMD_SET_DELAY_OFFSET:
      cmd_set_delay_offset();
      break;

    default:
      break;
  }
  _sTx.ack = _ack;
  _commnad = header->command;
}
