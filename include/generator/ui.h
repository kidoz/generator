#ifndef GENERATOR_UI_H
#define GENERATOR_UI_H

int ui_init(int argc, char *argv[]);
int ui_loop(void);
void ui_line(int line);
void ui_endfield(void);
void ui_final(void);
[[noreturn]] void ui_err(const char *text, ...);
void ui_musiclog(uint8 *data, unsigned int length);

#endif /* GENERATOR_UI_H */
