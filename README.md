# README

Version: 0.8

This repository contains the CPU design of [AUTD3](https://hapislab.org/airborne-ultrasound-tactile-display?lang=en).

# :fire: CAUTION

Some codes has omitted because they contain proprietary parts.

## Address map to FPGA

* BRAM_SELECT: High 2bit
* BRAM_ADDR: Low 14bit

| BRAM_SELECT | BRAM_ADDR | DATA                             | R/W | Note                                                       |
|-------------|-----------|----------------------------------|-----|------------------------------------------------------------|
| 0x0         | 0x0000    | Control flags and Clock property | R/W | 　                                                         |
| 　          | 0x0001    | LM Cycle                         | W   | 　                                                         |
| 　          | 0x0002    | LM Division                      | W   | 　                                                         |
| 　          | 0x0003    | LM clock initialize lap          | R   | 　                                                         |
| 　          | 0x0004    | LM calibration shift             | W   | 　                                                         |
| 　          | 0x0005    | LM bram addr offset              | W   | 　                                                         |
| 　          | 0x0006    | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x00FE    | Unused                           | 　  | 　                                                         |
| 　          | 0x00FF    | FPGA version number              | R   | 　                                                         |
| 　          | 0x0100    | nil                              | 　  | 　                                                         |
| 　          | ︙        | 　                               | 　  | 　                                                         |
| 　          | 0x3FFF    | nil                              | 　  | 　                                                         |
| 0x1         | 0x0000    | mod[1]/mod[0]                    | W   | 　                                                         |
| 　          | 0x0001    | mod[3]/mod[2]                    | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x07CF    | mod[3999]/mod[3998]              | W   | 　                                                         |
| 　          | 0x07D0    | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x07FF    | Unused                           | 　  | 　                                                         |
| 　          | 0x0800    | nil                              | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x3FFF    | nil                              | 　  | 　                                                         |
| 0x2         | 0x0000    | amp[0]/phase[0]                  | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x00F8    | amp[248]/phase[248]              | W   | 　                                                         |
| 　          | 0x00F9    | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x00FF    | Unused                           | 　  | 　                                                         |
| 　          | 0x0100    | delay[0]                         | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x01F8    | delay[248]                         | 　  | 　                                                         |
| 　          | 0x01F9    | Unused                         | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x01FF    | Unused                         | 　  | 　                                                         |
| 　          | 0x0200    | nil                              | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x3FFF    | nil                              | 　  | 　                                                         |
| 0x3         | 0x00000   | lm_x[0][15:0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(LM bram addr offset)*0x4000 |
| 　          | 0x00001   | lm_y[0][7:0]/lm_x[0][23:16]      | W   | 　                                                         |
| 　          | 0x00002   | lm_y[0][23:8]                    | W   | 　                                                         |
| 　          | 0x00003   | lm_z[0][15:0]                    | W   | 　                                                         |
| 　          | 0x00004   | lm_amp[0]/lm_z[0][23:16]         | W   | 　                                                         |
| 　          | 0x00005   | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x00007   | Unused                           | 　  | 　                                                         |
| 　          | 0x00008   | lm_x[1][15:0]                    | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x0000C   | lm_amp[1]/lm_z[1][23:16]         | W   | 　                                                         |
| 　          | 0x0000D   | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x0000F   | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x4E1F8   | lm_x[39999][15:0]                | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x4E1FC   | lm_amp[39999]/lm_z[39999][23:16] | W   | 　                                                         |
| 　          | 0x4E1FD   | Unused                           | 　  | 　                                                         |
| 　          | ︙        | ︙                               | 　  | 　                                                         |
| 　          | 0x4E1FF   | Unused                           | 　  | 　                                                         |

## Firmware version number

| Version number | Version | 
|----------------|---------| 
| 0              | v0.3 or former | 
| 1              | v0.4    | 
| 2              | v0.5    | 
| 3              | v0.6    | 
| 4              | v0.7    | 
| 5              | v0.8    | 

# Author

Shun Suzuki, 2020-
