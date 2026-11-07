#ifndef __IOMAP_H_
#define __IOMAP_H_

// Pinos do display OLED SSD1306 (I2C), copiados do projeto CEAMAX
// (main/hardware/iomap.h). Ajuste aqui se a sua fiacao for diferente.
#define IOMAP_SDA           48
#define IOMAP_SCL           47
#define IOMAP_I2C_NUM       0
#define IOMAP_I2C_FREQ      400000

// Teclado em matriz do CEAMAX (hardware/hardware_drv.h): Key_Sw1 fica na coluna 1 e
// Key_Sw2 na coluna 2; UP responde na linha 2 e DOWN na linha 1 (entradas com pull-down,
// so a coluna lida fica em nivel alto, as outras soltas).
#define IOMAP_COLUMN_1      4       // Key_Sw1
#define IOMAP_COLUMN_2      5       // Key_Sw2
#define IOMAP_KEY_LINE_1    41      // DOWN
#define IOMAP_KEY_LINE_2    42      // UP

#endif //__IOMAP_H_
