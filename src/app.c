/*
 * File: app.c
 * Project: app_src
 * Created Date: 29/06/2020
 * Author: Shun Suzuki
 * -----
 * Last Modified: 13/10/2021
 * Modified By: Shun Suzuki (suzuki@hapis.k.u-tokyo.ac.jp)
 * -----
 * Copyright (c) 2020-2021 Hapis Lab. All rights reserved.
 *
 */

#include "app.h"

#include "iodefine.h"
#include "utils.h"

#define CPU_VERSION (0x0013) /* v1.9 */

#define MICRO_SECONDS (1000)

#define SEQ_BUF_FOCI_SEGMENT_SIZE (0xFFF)
#define SEQ_BUF_GAIN_SEGMENT_SIZE (0x3F)
#define MOD_BUF_SEGMENT_SIZE (0x7FFF)

#define BRAM_CONFIG_SELECT (0)
#define BRAM_MOD_SELECT (1)
#define BRAM_TR_SELECT (2)
#define BRAM_SEQ_SELECT (3)

#define CONFIG_CTRL_FLAG (0x00)
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
#define CONFIG_CLK_INI_FLAG (0x13)
#define CONFIG_FPGA_VER (0x3F)

#define TR_DELAY_OFFSET_BASE_ADDR (0x100)

#define CP_MOD_INIT (0x0001)
#define CP_SEQ_INIT (0x0002)

#define OP_MODE_NORMAL (0)
#define SEQ_MODE_POINT (0)

#define MSG_CLEAR (0x00)
#define MSG_RD_CPU_V_LSB (0x01)
#define MSG_RD_CPU_V_MSB (0x02)
#define MSG_RD_FPGA_V_LSB (0x03)
#define MSG_RD_FPGA_V_MSB (0x04)

#define GAIN_DATA_MODE_PHASE_DUTY_FULL (0x0001)
#define GAIN_DATA_MODE_PHASE_FULL (0x0002)
#define GAIN_DATA_MODE_PHASE_HALF (0x0004)

extern RX_STR0 _sRx0;
extern RX_STR1 _sRx1;
extern TX_STR _sTx;

static volatile uint8_t _header_id = 0;
static volatile uint16_t _ctrl_flag = 0;
static volatile bool_t _read_fpga_info = false;

static volatile uint32_t _mod_cycle = 0;
static volatile uint32_t _mod_buf_fpga_write = 0;
static volatile bool_t _mod_buf_write_end = 0;

static volatile uint32_t _seq_cycle = 0;
static volatile uint32_t _seq_buf_fpga_write = 0;
static volatile bool_t _seq_buf_write_end = 0;
static volatile uint16_t _seq_gain_data_mode = GAIN_DATA_MODE_PHASE_DUTY_FULL;
static volatile uint16_t _seq_gain_size = 0;

static volatile uint16_t _ack = 0;

// fire when ethercat packet arrives
extern void recv_ethercat(void);
// fire once after power on
extern void init_app(void);
// fire periodically with 1ms interval
extern void update(void);

typedef enum {
  OUTPUT_ENABLE = 1 << 0,
  OUTPUT_BALANCE = 1 << 1,
  //
  SILENT = 1 << 3,
  FORCE_FAN = 1 << 4,
  OP_MODE = 1 << 5,
  SEQ_MODE = 1 << 6,
  //
} FPGAControlFlags;

typedef enum {
  MOD_BEGIN = 1 << 0,
  MOD_END = 1 << 1,
  SEQ_BEGIN = 1 << 2,
  SEQ_END = 1 << 3,
  READS_FPGA_INFO = 1 << 4,
  DELAY_OFFSET = 1 << 5,
} CPUControlFlags;

typedef struct {
  uint8_t msg_id;
  uint8_t fpga_ctrl_flags;
  uint8_t cpu_ctrl_flags;
  uint8_t mod_size;
  uint8_t mod[124];
} GlobalHeader;

inline static uint16_t get_cpu_version(void) { return CPU_VERSION; }
inline static uint16_t get_fpga_version(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_VER); }
inline static uint16_t read_fpga_info(void) { return bram_read(BRAM_CONFIG_SELECT, CONFIG_FPGA_INFO); }

static void clear(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;

  _ctrl_flag = SILENT;
  _read_fpga_info = false;
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CTRL_FLAG, _ctrl_flag);

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

static void write_mod(void) {
  GlobalHeader *header = (GlobalHeader *)(_sRx1.data);
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr;
  uint8_t i;
  uint16_t mod_div;
  uint32_t offset = 0;
  volatile uint16_t *mod;

  if ((header->cpu_ctrl_flags & MOD_BEGIN) != 0) {
    _mod_cycle = 0;
    _mod_buf_fpga_write = 0;
    _mod_buf_write_end = false;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_BRAM_OFFSET, 0);
    mod_div = (((uint16_t)header->mod[1] << 8) & 0xFF00) | (header->mod[0] & 0x00FF);
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_DIV, max(1, mod_div) - 1);
    offset += 2;
  }

  mod = (volatile uint16_t *)&header->mod[offset];
  for (i = 0; i < (header->mod_size >> 1); i++) {
    addr = get_addr(BRAM_MOD_SELECT, (_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) >> 1);
    word_cpy_volatile(&base[addr], &mod[i], 1);
    _mod_buf_fpga_write += 2;
    if ((_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) == 0) bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_BRAM_OFFSET, _mod_buf_fpga_write >> 15);
  }
  if ((header->mod_size & 0x1) != 0) {
    addr = get_addr(BRAM_MOD_SELECT, (_mod_buf_fpga_write & MOD_BUF_SEGMENT_SIZE) >> 1);
    word_cpy_volatile(&base[addr], &mod[i], 1);
    _mod_buf_fpga_write += 1;
  }
  _mod_cycle += header->mod_size;

  if ((header->cpu_ctrl_flags & MOD_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_MOD_CYCLE, max(1, _mod_cycle) - 1);
    _mod_buf_write_end = true;
  }
}

static void set_delay_offset(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_TR_SELECT, TR_DELAY_OFFSET_BASE_ADDR);
  word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
}

static void normal_op(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_TR_SELECT, 0);
  word_cpy_volatile(&base[addr], _sRx0.data, TRANS_NUM);
}

static void recv_point_seq(void) {
  GlobalHeader *header = (GlobalHeader *)(_sRx1.data);
  volatile uint16_t *base = (uint16_t *)FPGA_BASE;
  uint16_t seq_div;
  uint16_t wavelength;
  uint16_t seq_size = _sRx0.data[0];
  uint32_t offset = 1;
  volatile Focus *foci;
  uint16_t i, addr;

  if ((header->cpu_ctrl_flags & SEQ_BEGIN) != 0) {
    _seq_cycle = 0;
    _seq_buf_fpga_write = 0;
    _seq_buf_write_end = false;
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, 0);
    seq_div = _sRx0.data[1];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_DIV, max(1, seq_div) - 1);
    wavelength = _sRx0.data[2];
    bram_write(BRAM_CONFIG_SELECT, CONFIG_WAVELENGTH_UM, wavelength);
    offset += 4;
  }

  foci = (volatile Focus *)&_sRx0.data[offset];
  for (i = 0; i < seq_size; i++) {
    // 2bit left shift = *4=sizeof(Focus)/16
    addr = get_addr(BRAM_SEQ_SELECT, (_seq_buf_fpga_write & SEQ_BUF_FOCI_SEGMENT_SIZE) << 2);
    word_cpy_volatile(&base[addr], (volatile uint16_t *)&foci[i], sizeof(Focus) >> 1);
    _seq_buf_fpga_write++;
    // 12bit right shift = /SEQ_BUF_FOCI_SEGMENT_SIZE
    if ((_seq_buf_fpga_write & SEQ_BUF_FOCI_SEGMENT_SIZE) == 0) bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_BRAM_OFFSET, _seq_buf_fpga_write >> 12);
  }
  _seq_cycle += seq_size;

  if ((header->cpu_ctrl_flags & SEQ_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_CYCLE, max(1, _seq_cycle) - 1);
    _seq_buf_write_end = true;
  }
}

static void recv_gain_seq(void) {
  GlobalHeader *header = (GlobalHeader *)(_sRx1.data);
  volatile uint16_t *base = (uint16_t *)FPGA_BASE;
  uint16_t seq_div;
  uint16_t addr;
  uint8_t i;
  uint16_t duty = 0xFF00;
  uint16_t phase;

  if ((header->cpu_ctrl_flags & SEQ_BEGIN) != 0) {
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

  if ((header->cpu_ctrl_flags & SEQ_END) != 0) {
    bram_write(BRAM_CONFIG_SELECT, CONFIG_SEQ_CYCLE, max(1, _seq_gain_size) - 1);
    _seq_buf_write_end = true;
  }
}

inline static uint64_t get_next_sync0(void) {
  volatile uint64_t next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;
  volatile uint64_t sys_time = ECATC.DC_SYS_TIME.LONGLONG;
  while (next_sync0 < sys_time + 250 * MICRO_SECONDS) {
    sys_time = ECATC.DC_SYS_TIME.LONGLONG;
    if (sys_time > next_sync0) next_sync0 = ECATC.DC_CYC_START_TIME.LONGLONG;
  }
  return next_sync0;
}

static void init_mod_clk() {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_MOD_SYNC_TIME_BASE);
  uint64_t next_sync0 = get_next_sync0();
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CTRL_FLAG, CP_MOD_INIT | _ctrl_flag);
}

static void init_fpga_seq_clk(void) {
  volatile uint16_t *base = (volatile uint16_t *)FPGA_BASE;
  uint16_t addr = get_addr(BRAM_CONFIG_SELECT, CONFIG_SEQ_SYNC_TIME_BASE);
  uint64_t next_sync0 = get_next_sync0();
  word_cpy_volatile(&base[addr], (volatile uint16_t *)&next_sync0, sizeof(uint64_t));
  bram_write(BRAM_CONFIG_SELECT, CONFIG_CTRL_FLAG, CP_SEQ_INIT | _ctrl_flag);
}

void init_app(void) { clear(); }

void update(void) {
  if (_mod_buf_write_end) {
    _mod_buf_write_end = false;
    init_mod_clk();
  }

  if (_seq_buf_write_end) {
    _seq_buf_write_end = false;
    init_fpga_seq_clk();
  }

  switch (_header_id) {
    case MSG_RD_CPU_V_LSB:
    case MSG_RD_CPU_V_MSB:
    case MSG_RD_FPGA_V_LSB:
    case MSG_RD_FPGA_V_MSB:
      break;
    default:
      if (_read_fpga_info) _ack = (_ack & 0xFF00) | read_fpga_info();
      break;
  }

  _sTx.ack = _ack;
}

void recv_ethercat(void) {
  GlobalHeader *header = (GlobalHeader *)(_sRx1.data);
  if (header->msg_id == _header_id) return;
  _header_id = header->msg_id;
  _ack = ((uint16_t)(header->msg_id)) << 8;
  _read_fpga_info = (header->cpu_ctrl_flags & READS_FPGA_INFO) != 0;
  if (_read_fpga_info) _ack |= read_fpga_info();

  switch (_header_id) {
    case MSG_CLEAR:
      clear();
      break;
    case MSG_RD_CPU_V_LSB:
      _ack = (_ack & 0xFF00) | (get_cpu_version() & 0xFF);
      break;
    case MSG_RD_CPU_V_MSB:
      _ack = (_ack & 0xFF00) | ((get_cpu_version() >> 8) & 0xFF);
      break;
    case MSG_RD_FPGA_V_LSB:
      _ack = (_ack & 0xFF00) | (get_fpga_version() & 0xFF);
      break;
    case MSG_RD_FPGA_V_MSB:
      _ack = (_ack & 0xFF00) | ((get_fpga_version() >> 8) & 0xFF);
      break;
    default:
      _ctrl_flag = header->fpga_ctrl_flags;
      bram_write(BRAM_CONFIG_SELECT, CONFIG_CTRL_FLAG, _ctrl_flag);
      write_mod();
      if ((header->cpu_ctrl_flags & DELAY_OFFSET) != 0) {
        set_delay_offset();
      } else if ((header->fpga_ctrl_flags & OP_MODE) == OP_MODE_NORMAL) {
        normal_op();
      } else {
        if ((header->fpga_ctrl_flags & OP_MODE) == SEQ_MODE_POINT)
          recv_point_seq();
        else
          recv_gain_seq();
      }
      break;
  }
  _sTx.ack = _ack;
}
