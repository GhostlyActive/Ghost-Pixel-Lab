#include "editor.h"

namespace apps::ghost {

bool Editor::key(uint8_t k, char* outLine, int outSize) {
    switch (k) {
    case KEY_RETURN: {
        // Re-read the whole logical line under the cursor (it may span two
        // physical rows), then drop below its last row so program output or
        // READY. prints on the following line.
        int last = screen_.readLogicalLine(screen_.cursorY(), outLine, outSize);
        screen_.setCursor(0, last);
        screen_.newLine();
        return true;
    }

    case KEY_DELETE:
        screen_.put(0x14);            // Screen handles the backspace
        return false;

    case KEY_CRSR_RIGHT: {
        int x = screen_.cursorX() + 1, y = screen_.cursorY();
        if (x >= Screen::COLS) { x = 0; ++y; }
        screen_.setCursor(x, y);
        return false;
    }
    case KEY_CRSR_LEFT: {
        int x = screen_.cursorX() - 1, y = screen_.cursorY();
        if (x < 0) { x = Screen::COLS - 1; --y; }
        screen_.setCursor(x, y);
        return false;
    }
    case KEY_CRSR_DOWN:
        screen_.setCursor(screen_.cursorX(), screen_.cursorY() + 1);
        return false;
    case KEY_CRSR_UP:
        screen_.setCursor(screen_.cursorX(), screen_.cursorY() - 1);
        return false;

    case KEY_HOME:
        screen_.home();
        return false;
    case KEY_CLR:
        screen_.reset();
        return false;

    default:
        // Printable characters echo to the screen and advance the cursor.
        if (k >= 0x20 && k < 0x80) screen_.put(k);
        return false;
    }
}

} // namespace apps::ghost
