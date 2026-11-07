#include "oled_port.h"
#include "esp_random.h"
#include "screens/ssd1306_generic.h"

void oled_clear(void)
{
    ssd1306_Fill(Black);
}

void oled_pixel(int x, int y, int on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    ssd1306_DrawPixel((uint8_t)x, (uint8_t)y, on ? White : Black);
}

void oled_flush(void)
{
    ssd1306_UpdateScreen();
}

uint32_t oled_rand(void)
{
    return esp_random();
}
