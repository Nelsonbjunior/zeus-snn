#ifndef __I2CDRV_H_
#define __I2CDRV_H_

#include "esp_err.h"

esp_err_t i2cDrv_init(uint8_t SdaPin, uint8_t SclPin, uint32_t Frequency, uint8_t I2cNum);
esp_err_t i2cDrv_write(uint8_t slave_addr, uint8_t* buffer_ptr, uint32_t buffer_size, uint8_t I2cNum);

#endif //__I2CDRV_H_