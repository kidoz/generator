#ifndef GENERATOR_UI_H
#define GENERATOR_UI_H

/* The contract every UI backend implements: main() calls ui_init(), then
 * ui_loop(), and ui_final() on the way out. ui_err() is the fatal-error
 * path; src/ui/common/ui_error.cpp provides it for backends with no error
 * UI of their own.
 *
 * The per-scanline callbacks (ui_line, ui_endfield, ui_musiclog) used to
 * live here too. The core now emits through IVideoBackend/IAudioBackend on
 * the System aggregate instead, so nothing outside a backend called them;
 * a backend that still wants such a function declares it itself. */

int ui_init(int argc, char *argv[]);
int ui_loop(void);
void ui_final(void);
[[noreturn]] void ui_err(const char *text, ...);

#endif /* GENERATOR_UI_H */
