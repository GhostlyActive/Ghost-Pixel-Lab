// The screen editor: turns key events into cursor moves and characters on the
// Screen, and on RETURN re-reads the whole physical line the cursor sits on
// and hands it to BASIC. That "the screen is the input buffer" behaviour is
// the heart of how a C64 feels — you can cursor up to an old line, change it,
// press RETURN, and it is re-entered.
//
// Keys arrive as PETSCII-ish codes so the (future) Bluetooth keyboard driver
// and the host test harness feed the exact same entry point.
#pragma once

#include "screen.h"

#include <cstdint>

namespace apps::ghost {

// Control codes the editor understands (PETSCII values).
enum Key : uint8_t {
    KEY_RETURN      = 0x0D,
    KEY_DELETE      = 0x14,   // backspace / INST-DEL
    KEY_CRSR_DOWN   = 0x11,
    KEY_CRSR_UP     = 0x91,
    KEY_CRSR_RIGHT  = 0x1D,
    KEY_CRSR_LEFT   = 0x9D,
    KEY_HOME        = 0x13,
    KEY_CLR         = 0x93,   // shift+HOME: clear screen
};

class Editor {
public:
    explicit Editor(Screen& screen) : screen_(screen) {}

    // Feed one key. Printable PETSCII codes are echoed; control codes move the
    // cursor. On RETURN the physical line under the cursor is read back into
    // `outLine` and true is returned, so the caller can hand it to BASIC.
    bool key(uint8_t k, char* outLine, int outSize);

private:
    Screen& screen_;
};

} // namespace apps::ghost
