// FT3168 capacitive touch controller on I2C 0x38.
#pragma once

#include <cstdint>

namespace board::touch {

struct Point {
    int16_t x;
    int16_t y;
    bool    pressed;
};

bool  begin();
Point read();

} // namespace board::touch
