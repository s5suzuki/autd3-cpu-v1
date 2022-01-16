# AUTD3 CPU firmware

Version: 1.11

This repository contains the CPU design of [AUTD3](https://hapislab.org/airborne-ultrasound-tactile-display?lang=en).

# :fire: CAUTION

Some codes has omitted because they contain proprietary parts.

## Address map to FPGA

* BRAM_SELECT: High 2bit
* BRAM_ADDR: Low 14bit

| BRAM_SELECT | BRAM_ADDR | DATA                             | R/W | Note                                                       |
|-------------|-----------|----------------------------------|-----|------------------------------------------------------------|
| 0x0         | 0x0000    | Control flags                    | W |                                                          |
|           | 0x0001    | FPGA info                         | W   |                                                          |
|           | 0x0002    | Sequence cycle                         | W   |                                                          |
|           | 0x0003    | Sequence freq division ratio                | W   |                                                          |
|           | 0x0004    | -                                 | -   |                                                          |
|           | 0x0005    | -                                 | -   |                                                          |
|           | 0x0006    | Modulation bram addr offset              | W  |                                                          |
|           | 0x0007    | Sequence bram addr offset              | W  |                                                          |
|           | 0x0008    | Wavelength in micro meters        | W  |                                                          |
|           | 0x0009    | Sequence clock sync time[15:0]           | W  |                                                          |
|           | 0x000A    | Sequence clock sync time[31:16]          | W  |                                                          |
|           | 0x000B    | Sequence clock sync time[47:32]          | W  |                                                          |
|           | 0x000C    | Sequence clock sync time[63:48]          | W  |                                                          |
|           | 0x000D    | Modulation cycle                  | W  |                                                          |
|           | 0x000E    | Modulation freq division ratio         | W  |                                                          |
|           | 0x000F    | Modulation clock sync time[15:0]           | W  |                                                          |
|           | 0x0010    | Modulation clock sync time[31:16]          | W  |                                                          |
|           | 0x0011    | Modulation clock sync time[47:32]          | W  |                                                          |
|           | 0x0012    | Modulation clock sync time[63:48]          | W  |                                                          |
|           | 0x0013    | clock init flags                           | W  |                                                          |
|           | 0x0014    | Silent step                           | W  |                                                          |
|           | 0x0015    | unused                           | -  |                                                          |
|           | ︙        | ︙                               | ︙ |                                                          |
|           | 0x003E    | unused                           | -  |                                                          |
|           | 0x003F    | FPGA version number              | R   |                                                          |
|           | 0x0040    | nil                              | -  |                                                          |
|           | ︙        |                                | ︙  |                                                          |
|           | 0x3FFF    | nil                              | -  |                                                          |
| 0x1         | 0x0000    | mod[1]/mod[0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(Modulation bram addr offset)*0x4000                                                         |
|           | 0x0001    | mod[3]/mod[2]                    | W   |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x7FFF    | mod[65535]/mod[65534]              | W   |                                                          |
| 0x2         | 0x0000    | duty[0]/phase[0]                  | W   |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x00F8    | duty[248]/phase[248]              | W   |                                                          |
|           | 0x00F9    | unused                           | -  |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x00FF    | unused                         | -  |                                                          |
|             | 0x0100    | duty_offset[0]/delay_reset[0]/delay[0]                  | W   |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x01F8    | duty_offset[248]/delay_reset[248]/delay[248]              | W   |                                                          |
|           | 0x01F9    | delay reset                           | W  |                                                          |
|           | 0x01FA    | unused                           | -  |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x01FF    | unused                         | -  |                                                          |
|           | 0x0200    | nil                              | -  |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x3FFF    | nil                              | -  |                                                          |

### Sequence data mode == 0 

| BRAM_SELECT | BRAM_ADDR | DATA                             | R/W | Note                                                       |
|-------------|-----------|----------------------------------|-----|------------------------------------------------------------|
| 0x3         | 0x00000   | x[0][15:0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(Sequence bram addr offset)*0x4000 |
|           | 0x00001   | y[0][13:0], x[0][17:16]      | W   |                                                          |
|           | 0x00002   | z[0][11:0], y[0][17:14]                    | W   |                                                          |
|           | 0x00003   | 0b00, duty[0], z[0][17:12]                    | W   |                                                          |
|           | 0x00004   | x[1][15:0]                    | W   |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x3FFFC   | x[65535][15:0]                | W   |                                                          |
|           | ︙        | ︙                               | ︙  |                                                          |
|           | 0x3FFFF   | 0b00, duty[65535], z[39999][17:12] | W   |                                                          |

### Sequence data mode == 1

| BRAM_SELECT | BRAM_ADDR | DATA                             | R/W | Note                                                       |
|-------------|-----------|----------------------------------|-----|------------------------------------------------------------|
| 0x3         | 0x00000   | duty[0][0]/phase[0][0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(Sequence bram addr offset)*0x4000 |
|           | 0x00001   | duty[0][1]/phase[0][1]                     | W   |                                                          |
|           | ︙   | ︙                    | ︙   |                                                          |
|           | 0x000F8   | duty[0][248]/phase[0][248]                     | W   |                                                          |
|           | 0x000F9   | unused                     | -   |                                                          |
|           | ︙   | ︙                    | ︙   |                                                          |
|           | 0x000FF   | unused                     | -   |                                                          |
|             | 0x00100   | duty[1][0]/phase[1][0]                    | W   |  |
|           | ︙   | ︙                    | ︙   |                                                          |
|           | 0x7FFF8   | duty[2047][248]/phase[2047][248]                     | W   |                                                          |
|           | 0x7FFF9   | unused                     | -  |                                                          |
|           | ︙   | ︙                    | ︙   |                                                          |
|           | 0x7FFFF   | unused                     | -   |                                                          |

## Firmware version number

| Version number | Version | 
|----------------|---------| 
| 0x0000 (0)         | v0.3 or former | 
| 0x0001 (1)         | v0.4    | 
| 0x0002 (2)         | v0.5    | 
| 0x0003 (3)         | v0.6    | 
| 0x0004 (4)         | v0.7    | 
| 0x0005 (5)         | v0.8    | 
| 0x0006 (6)         | v0.9    | 
| 0x000A (10)         | v1.0    | 
| 0x000B (11)         | v1.1    | 
| 0x000C (12)         | v1.2    | 
| 0x000D (13)         | v1.3    | 
| 0x0010 (16)         | v1.6    | 
| 0x0011 (17)         | v1.7    | 
| 0x0012 (18)         | v1.8    | 
| 0x0013 (19)         | v1.9    | 
| 0x0014 (20)         | v1.10    | 
| 0x0015 (21)         | v1.11    | 

# Author

Shun Suzuki, 2020-2022
