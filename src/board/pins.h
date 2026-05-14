// Pin map for the Waveshare ESP32-S3-Touch-AMOLED-1.8.
// Source: official board schematic.
#pragma once

namespace board::pins {

// I2C bus shared by AXP2101, QMI8658, TCA9554, ES8311, PCF85063, FT3168.
inline constexpr int I2C_SDA = 15;
inline constexpr int I2C_SCL = 14;

// SH8601 1.8" AMOLED, 368x448, driven over QSPI.
inline constexpr int LCD_CS    = 12;
inline constexpr int LCD_SCK   = 11;
inline constexpr int LCD_D0    = 4;
inline constexpr int LCD_D1    = 5;
inline constexpr int LCD_D2    = 6;
inline constexpr int LCD_D3    = 7;
inline constexpr int LCD_TE    = 13;     // tearing-effect signal (unused here)
inline constexpr int LCD_W     = 368;
inline constexpr int LCD_H     = 448;

// FT3168 capacitive touch controller.
inline constexpr int TP_INT    = 21;

// I2S audio (ES8311 mono codec).
inline constexpr int I2S_MCK   = 16;
inline constexpr int I2S_BCK   = 9;
inline constexpr int I2S_WS    = 45;
inline constexpr int I2S_DI    = 10;   // mic data, codec -> MCU
inline constexpr int I2S_DO    = 8;    // speaker data, MCU -> codec
inline constexpr int PA_EN     = 46;   // Class-D amp enable

// 1-bit SD-MMC.
inline constexpr int SD_CLK    = 2;
inline constexpr int SD_CMD    = 1;
inline constexpr int SD_DATA   = 3;

// TCA9554 I/O-expander (address 0x20). EXIO line mapping per schematic.
inline constexpr uint8_t TCA9554_ADDR  = 0x20;
inline constexpr uint8_t EXIO_LCD_RST  = 0;     // active low
inline constexpr uint8_t EXIO_TP_RST   = 2;     // active low

// I2C device addresses (7-bit).
inline constexpr uint8_t AXP2101_ADDR  = 0x34;
inline constexpr uint8_t QMI8658_ADDR  = 0x6B;
inline constexpr uint8_t ES8311_ADDR   = 0x18;
inline constexpr uint8_t PCF85063_ADDR = 0x51;
inline constexpr uint8_t FT3168_ADDR   = 0x38;

} // namespace board::pins
