/**
 * 
 * @file ssd1306_generic.c
 * @date 21/06/2012
 * @author Nelson Barbosa dos Santos Jr
 * @copyright ECP 2012 - Fucapi
 * @brief
 * @note
 * 1- Biblioteca baseada na biblioteca de referencia abaixo, originalmente escrita para STM32.
 * 2- Alterações para comportar qualquer tamanho de caracter.
 * 
 * This Library was originally written by Olivier Van den Eede (4ilo) in 2016.
 * Some refactoring was done and SPI support was added by Aleksander Alekseev (afiskon) in 2018.
 *
 * https://github.com/afiskon/stm32-ssd1306
 */

#include "ssd1306_generic.h"
#include <string.h>

#define TAG "ssd1306.h"

static uint8_t send_buf[SSD1306_WIDTH*8 + 1];

void ssd1306_Reset(void) {
	/* for I2C - do nothing */
}

// Send a byte to the command register
void ssd1306_WriteCommand(uint8_t byte) {
	send_buf[0] = 0x00;
	send_buf[1] = byte;
	i2c_master_write_to_device(0, SSD1306_I2C_ADDR, send_buf, 2, 100 / portTICK_PERIOD_MS);
}

// Send data
void ssd1306_WriteData(uint8_t* buffer, uint8_t buff_size) {
	uint8_t i = buff_size;
	while(i > 0){
		send_buf[i] = buffer[i-1];
		i--;
	}
	send_buf[0] = 0x40;
	i2c_master_write_to_device(0, SSD1306_I2C_ADDR, send_buf, buff_size + 1, 100 / portTICK_PERIOD_MS);
}

// Screenbuffer
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

void ssd1306_CopyBuffer(uint8_t *destination, size_t length) {
    if (destination == NULL) return;
    if (length > sizeof(SSD1306_Buffer)) length = sizeof(SSD1306_Buffer);
    memcpy(destination, SSD1306_Buffer, length);
}

// Screen object
static SSD1306_t SSD1306;

// Initialize the oled screen
void ssd1306_Init(void) {
	// Reset OLED
	ssd1306_Reset();

	// Wait for the screen to boot
	vTaskDelay(200 / portTICK_PERIOD_MS);

	// Init OLED
	ssd1306_WriteCommand(0xAE); //display off

	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x20); //Set Memory Addressing Mode   
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x00); // 00b,Horizontal Addressing Mode; 01b,Vertical Addressing Mode;
	vTaskDelay(1 / portTICK_PERIOD_MS);             // 10b,Page Addressing Mode (RESET); 11b,Invalid
	ssd1306_WriteCommand(0xB0); //Set Page Start Address for Page Addressing Mode,0-7
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#ifdef SSD1306_MIRROR_VERT
	ssd1306_WriteCommand(0xC0); // Mirror vertically
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	ssd1306_WriteCommand(0xC8); //Set COM Output Scan Direction
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#endif
	ssd1306_WriteCommand(0x00); //---set low column address
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x10); //---set high column address
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x40); //--set start line address - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x81); //--set contrast control register - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xFF);
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#ifdef SSD1306_MIRROR_HORIZ
	ssd1306_WriteCommand(0xA0); // Mirror horizontally
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	ssd1306_WriteCommand(0xA1); //--set segment re-map 0 to 127 - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#endif
	#ifdef SSD1306_INVERSE_COLOR
	ssd1306_WriteCommand(0xA7); //--set inverse color
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	ssd1306_WriteCommand(0xA6); //--set normal color
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#endif

	// Set multiplex ratio.
	#if (SSD1306_HEIGHT == 128)
	// Found in the Luma Python lib for SH1106.
	ssd1306_WriteCommand(0xFF);
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	ssd1306_WriteCommand(0xA8); //--set multiplex ratio(1 to 64) - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#endif

	#if (SSD1306_HEIGHT == 32)
	ssd1306_WriteCommand(0x1F); //
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#elif (SSD1306_HEIGHT == 64)
	ssd1306_WriteCommand(0x3F); //
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#elif (SSD1306_HEIGHT == 128)
	ssd1306_WriteCommand(0x3F); // Seems to work for 128px high displays too.
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	#error "Only 32, 64, or 128 lines of height are supported!"
	#endif

	ssd1306_WriteCommand(0xA4); //0xa4,Output follows RAM content;0xa5,Output ignores RAM content
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xD3); //-set display offset - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x00); //-not offset
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xD5); //--set display clock divide ratio/oscillator frequency
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xF0); //--set divide ratio
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xD9); //--set pre-charge period
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x22); //
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xDA); //--set com pins hardware configuration - CHECK
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#if (SSD1306_HEIGHT == 32)
	ssd1306_WriteCommand(0x02);
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#elif (SSD1306_HEIGHT == 64)
	ssd1306_WriteCommand(0x12);
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#elif (SSD1306_HEIGHT == 128)
	ssd1306_WriteCommand(0x12);
	vTaskDelay(1 / portTICK_PERIOD_MS);
	#else
	#error "Only 32, 64, or 128 lines of height are supported!"
	#endif

	ssd1306_WriteCommand(0xDB); //--set vcomh
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x20); //0x20,0.77xVcc
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x8D); //--set DC-DC enable
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0x14); //
	vTaskDelay(1 / portTICK_PERIOD_MS);
	ssd1306_WriteCommand(0xAF); //--turn on SSD1306 panel
	vTaskDelay(1 / portTICK_PERIOD_MS);
	// Clear screen
	ssd1306_Fill(Black);

	// Flush buffer to screen
	ssd1306_UpdateScreen();

	// Set default values for screen object
	SSD1306.CurrentX = 0;
	SSD1306.CurrentY = 0;

	SSD1306.Initialized = 1;
}

// Fill the whole screen with the given color
void ssd1306_Fill(SSD1306_COLOR color) {
    /* Set memory */
    uint32_t i;

    for(i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = (color == Black) ? 0x00 : 0xFF;
    }
}

// Write the screenbuffer with changed to the screen
void ssd1306_UpdateScreen(void) {
		uint8_t i = 0;
    // Write data to each page of RAM. Number of pages
    // depends on the screen height:
    //
    //  * 32px   ==  4 pages
    //  * 64px   ==  8 pages
    //  * 128px  ==  16 pages
    for( i = 0; i < SSD1306_HEIGHT/8; i++) {
			ssd1306_WriteCommand(0xB0 + i); // Set the current RAM page address.
			//vTaskDelay(1 / portTICK_PERIOD_MS);
			ssd1306_WriteCommand(0x00);
			//vTaskDelay(1 / portTICK_PERIOD_MS);
			ssd1306_WriteCommand(0x10);
			//vTaskDelay(1 / portTICK_PERIOD_MS);
			ssd1306_WriteData(&SSD1306_Buffer[SSD1306_WIDTH*i],SSD1306_WIDTH);
			//vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

//    Draw one pixel in the screenbuffer
//    X => X Coordinate
//    Y => Y Coordinate
//    color => Pixel color
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color) {
    if(x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        // Don't write outside the buffer
        return;
    }
    
    // Check if pixel should be inverted
    if(SSD1306.Inverted) {
        color = (SSD1306_COLOR)!color;
    }
    
    // Draw in the right color
    if(color == White) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
    } else { 
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

void ssd1306_DrawBitmap(uint8_t x, uint8_t y, uint8_t size_x, uint8_t size_y, const uint16_t *buffer){
	uint32_t nBytes = 0;
	uint32_t i, b, j, j_offset, byte_number;

	nBytes = (size_x / 16);
    if((size_x % 16) != 0)
	{
		nBytes++;
	}

	for(i = 0; i < size_y; i++) 
	{
		for(byte_number = 0; byte_number < nBytes; byte_number++)
		{
			b = buffer[(i*nBytes + byte_number)];			
			for(j = 0; j < 16; j++)
			{
				//ESP_LOGI(TAG, "J= %04x", j);
				j_offset = j + 16 * (byte_number);
				if((b << j) & 0x8000)
				{
					ssd1306_DrawPixel(x + j_offset, (y + i), (SSD1306_COLOR) 1);
				}
				else
				{
					ssd1306_DrawPixel(x + j_offset, (y + i), (SSD1306_COLOR)0);
				}
			}
		}
    }
	return;
}

// Draw 1 char to the screen buffer
// ch       => char om weg te schrijven
// Font     => Font waarmee we gaan schrijven
// color    => Black or White
char ssd1306_WriteChar(char ch, FontDef Font, SSD1306_COLOR color) {
    uint32_t i, b, j, j_offset, byte_number;
    
    // Check if character is valid
    if (ch < 32 || ch > 126)
        return 0;

    
    // Check remaining space on current line
    //if (SSD1306_WIDTH < (SSD1306.CurrentX + Font.FontWidth) ||
    //    SSD1306_HEIGHT < (SSD1306.CurrentY + Font.FontHeight))
    //{
    //    // Not enough space on current line
	//	ESP_LOGI(TAG, "Not enough space on current line");
    //    return 0;
    //}
    
    // Use the font to write

    for(i = 0; i < Font.FontHeight; i++) 
	{
		for(byte_number = 1; byte_number <= Font.DataSize; byte_number++)
		{
			b = Font.data[(ch - 32) * Font.FontHeight * (Font.DataSize) + (i*Font.DataSize + byte_number) - 1];
			
			//ESP_LOGI(TAG, "%04x numb=%02x i=%02x", ((ch - 32) * Font.FontHeight * (Font.DataSize) + (i*Font.DataSize + byte_number) - 1), byte_number,i);
			for(j = 0; j < Font.FontWidth; j++)
			{
				j_offset = j + 16 * (byte_number-1);
				if((b << j) & 0x8000)
				{
					ssd1306_DrawPixel(SSD1306.CurrentX + j_offset, (SSD1306.CurrentY + i), (SSD1306_COLOR) color);
					
				}
				else
				{
					ssd1306_DrawPixel(SSD1306.CurrentX + j_offset, (SSD1306.CurrentY + i), (SSD1306_COLOR)!color);
				}
			}
		}
    }
    
    // The current space is now taken
    SSD1306.CurrentX += Font.FontWidth;
    
    // Return written char for validation
    return ch;
}

// Write full string to screenbuffer
char ssd1306_WriteString(char* str, FontDef Font, SSD1306_COLOR color) {
    // Write until null-byte
    while (*str) {
        if (ssd1306_WriteChar(*str, Font, color) != *str) {
            // Char could not be written
            return *str;
        }
        
        // Next char
        str++;
    }
    
    // Everything ok
    return *str;
}

// Position the cursor
void ssd1306_SetCursor(uint8_t x, uint8_t y) {
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

// Draw line by Bresenhem's algorithm
void ssd1306_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color) {
  int32_t deltaX = 	(x2 - x1);
  int32_t deltaY = abs(y2 - y1);
  int32_t signX = ((x1 < x2) ? 1 : -1);
  int32_t signY = ((y1 < y2) ? 1 : -1);
  int32_t error = deltaX - deltaY;
  int32_t error2;
	
  ssd1306_DrawPixel(x2, y2, color);
	while((x1 != x2) || (y1 != y2))
	{
    ssd1306_DrawPixel(x1, y1, color);
    error2 = error * 2;
    if(error2 > -deltaY)
    {
      error -= deltaY;
      x1 += signX;
    }
    else
    {
    /*nothing to do*/
    }
		
    if(error2 < deltaX)
    {
      error += deltaX;
      y1 += signY;
    }
    else
    {
    /*nothing to do*/
    }
  }
  return;
}

//Draw rectangle
void ssd1306_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color, uint8_t fill) {
  
	if(fill == 0){
		ssd1306_Line(x1,y1,x2,y1,color);
		ssd1306_Line(x2,y1,x2,y2,color);
		ssd1306_Line(x1,y2,x2,y2,color);
		ssd1306_Line(x1,y1,x1,y2,color);
		}
  else{
		while(y1 < y2){
			ssd1306_Line(x1,y1,x2,y1,color);
			y1++;
		}
  }

}

