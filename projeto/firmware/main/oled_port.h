#ifndef __OLED_PORT_H_
#define __OLED_PORT_H_

#include <stdint.h>

// Camada fina entre o rosto (zeus_face.c) e o display. No ESP32 vai para o
// driver SSD1306 (oled_port.c); no simulador do PC (sim/) vai para um buffer
// que vira PNG. Assim o mesmo codigo de desenho roda nos dois lugares.

#define OLED_W  128
#define OLED_H  64

void oled_clear(void);
void oled_pixel(int x, int y, int on);
void oled_flush(void);
uint32_t oled_rand(void);

#endif //__OLED_PORT_H_
