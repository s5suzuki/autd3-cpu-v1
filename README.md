# AUTD3 CPU firmware

Version: 1.2

This repository contains the CPU design of [AUTD3](https://hapislab.org/airborne-ultrasound-tactile-display?lang=en).

# :fire: CAUTION

Some codes has omitted because they contain proprietary parts.

## Address map to FPGA

* BRAM_SELECT: High 2bit
* BRAM_ADDR: Low 14bit

| BRAM_SELECT | BRAM_ADDR | DATA                             | R/W | Note                                                       |
|-------------|-----------|----------------------------------|-----|------------------------------------------------------------|
| 0x0         | 0x0000    | Control flags and Clock property | R/W | 　                                                         |
| 　          | 0x0001    | FPGA info                         | W   | 　                                                         |
| 　          | 0x0002    | Seq cycle                         | W   | 　                                                         |
| 　          | 0x0003    | Seq clock division                | W   | 　                                                         |
| 　          | 0x0004    | -                                 | -   | 　                                                         |
| 　          | 0x0005    | -                                 | -   | 　                                                         |
| 　          | 0x0006    | Mod bram addr offset              | W  | 　                                                         |
| 　          | 0x0007    | Seq bram addr offset              | W  | 　                                                         |
| 　          | 0x0008    | Wavelength in micro meters        | W  | 　                                                         |
| 　          | 0x0009    | Seq clk sync time[15:0]           | W  | 　                                                         |
| 　          | 0x000A    | Seq clk sync time[31:16]          | W  | 　                                                         |
| 　          | 0x000B    | Seq clk sync time[47:32]          | W  | 　                                                         |
| 　          | 0x000C    | Seq clk sync time[63:48]          | W  | 　                                                         |
| 　          | 0x000D    | Modulation cycle                  | W  | 　                                                         |
| 　          | 0x000E    | Modulation clock division         | W  | 　                                                         |
| 　          | 0x000F    | Mod clk sync time[15:0]           | W  | 　                                                         |
| 　          | 0x0010    | Mod clk sync time[31:16]          | W  | 　                                                         |
| 　          | 0x0011    | Mod clk sync time[47:32]          | W  | 　                                                         |
| 　          | 0x0012    | Mod clk sync time[63:48]          | W  | 　                                                         |
| 　          | 0x0013    | unused                           | -  | 　                                                         |
| 　          | ︙        | ︙                               | ︙ | 　                                                         |
| 　          | 0x003E    | unused                           | ︙  | 　                                                         |
| 　          | 0x003F    | FPGA version number              | R   | 　                                                         |
| 　          | 0x0040    | nil                              | -  | 　                                                         |
| 　          | ︙        | 　                               | ︙　  | 　                                                         |
| 　          | 0x3FFF    | nil                              | -　  | 　                                                         |
| 0x1         | 0x0000    | mod[1]/mod[0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(Mod bram addr offset)*0x4000　                                                         |
| 　          | 0x0001    | mod[3]/mod[2]                    | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x7FFF    | mod[65535]/mod[65534]              | W   | 　                                                         |
| 0x2         | 0x0000    | duty[0]/phase[0]                  | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x00F8    | duty[248]/phase[248]              | W   | 　                                                         |
| 　          | 0x00F9    | unused                           | 　-  | 　                                                         |
| 　          | ︙        | ︙                               | ︙　  | 　                                                         |
| 　          | 0x00FF    | unused                         | -　  | 　                                                         |
|             | 0x0100    | enable[0]/delay[0]                  | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x01F8    | enable[248]/delay[248]              | W   | 　                                                         |
| 　          | 0x01F9    | global enable                           | W　  | 　                                                         |
| 　          | 0x01FA    | unused                           | -　  | 　                                                         |
| 　          | ︙        | ︙                               | 　︙  | 　                                                         |
| 　          | 0x01FF    | unused                         | 　-  | 　                                                         |
| 　          | 0x0200    | nil                              | -　  | 　                                                         |
| 　          | ︙        | ︙                               | 　︙  | 　                                                         |
| 　          | 0x3FFF    | nil                              | -　  | 　                                                         |
| 0x3         | 0x00000   | lm_x[0][15:0]                    | W   | Below, the write address in the FPGA will be BRAM_ADDR+(Seq bram addr offset)*0x4000 |
| 　          | 0x00001   | lm_y[0][13:0], lm_x[0][17:16]      | W   | 　                                                         |
| 　          | 0x00002   | lm_z[0][11:0], lm_y[0][17:14]                    | W   | 　                                                         |
| 　          | 0x00003   | 0b00, lm_duty[0], lm_z[0][17:12]                    | W   | 　                                                         |
| 　          | 0x00004   | lm_x[1][15:0]                    | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x3FFFC   | lm_x[65535][15:0]                | W   | 　                                                         |
| 　          | ︙        | ︙                               | ︙  | 　                                                         |
| 　          | 0x3FFFF   | 0b00, lm_duty[65535], lm_z[39999][17:12] | W   | 　                                                         |

## Firmware version number

| Version number | Version | 
|----------------|---------| 
| 0x0000              | v0.3 or former | 
| 0x0001              | v0.4    | 
| 0x0002              | v0.5    | 
| 0x0003              | v0.6    | 
| 0x0004              | v0.7    | 
| 0x0005              | v0.8    | 
| 0x0006              | v0.9    | 
| 0x000A              | v1.0    | 
| 0x000B              | v1.1    | 
| 0x000C              | v1.2    | 

# Author

Shun Suzuki, 2020-2021
