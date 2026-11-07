#include <stdint.h>

#ifndef __SSD1306_FONTS_H__
#define __SSD1306_FONTS_H__

typedef struct 
{
	const uint8_t FontWidth;    /*!< Font width in pixels */
	uint8_t FontHeight;   		/*!< Font height in pixels */
	uint8_t DataSize;			/*!< Data size, in bytes*/
	const uint16_t *data; 		/*!< Pointer to data font data array */
} FontDef;

typedef struct
{
	const uint8_t BitmapWidth;		/*!< Bitmap width in pixels */
	const uint8_t BitmapHeight;		/*!< Bitmap height in pixels */
	const uint16_t *BitmapData;		/*!<  Pointer to data font data array*/
} BitmapDef;

//extern FontDef Font_6x8;
extern FontDef Font_7x10;
extern FontDef Font_8x14;
extern FontDef Font_11x18;
extern FontDef Font_16x26;
extern FontDef Font_31x50;

extern BitmapDef Bitmap_TelaInicial;
extern BitmapDef Bitmap_BarraLateral;
extern BitmapDef Bitmap_CursorLateral;
extern BitmapDef Bitmap_SetaInferior;
extern BitmapDef Bitmap_SemAlcance;
extern BitmapDef Bitmap_LowBatteryWarning;
extern BitmapDef Bitmap_ModuleOff;
extern BitmapDef Bitmap_PressureSymbol;
extern BitmapDef Bitmap_EmptyBattery;
extern BitmapDef Bitmap_QuarterBattery;
extern BitmapDef Bitmap_FullBattery;
extern BitmapDef Bitmap_MediumBattery;
extern BitmapDef Bitmap_ThreeQuarterBattery;
extern BitmapDef Bitmap_BitmapChargerBattery;
extern BitmapDef Bitmap_BLEConnected;
extern BitmapDef Bitmap_WaitingInfo;
extern BitmapDef Bitmap_TratorPulver;
extern BitmapDef Bitmap_BatteryWarning;
extern BitmapDef Bitmap_Ligth;
extern BitmapDef Bitmap_Traco;
extern BitmapDef Bitmap_X;
extern BitmapDef Bitmap_Ligth_Icon;
extern BitmapDef Bitmap_Compressor;
extern BitmapDef Bitmap_Pressure;
extern BitmapDef Bitmap_Up_Arrow; 
extern BitmapDef Bitmap_Down_Arrow;
extern BitmapDef Bitmap_Fan;
extern BitmapDef Bitmap_ON;
extern BitmapDef Bitmap_OFF;


#endif // __SSD1306_FONTS_H__
