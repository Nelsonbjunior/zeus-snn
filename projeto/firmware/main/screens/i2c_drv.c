#include "./screens/i2c_drv.h"
#include "driver/i2c.h"
#include "esp_log.h"

#define LOG_TAG "i2c_drv.h"

esp_err_t i2cDrv_init(uint8_t SdaPin, uint8_t SclPin, uint32_t Frequency, uint8_t I2cNum)
{
    i2c_config_t i2c_config = 
    {
        .mode=I2C_MODE_MASTER,
        .sda_io_num=SdaPin,
        .scl_io_num=SclPin, 
        .sda_pullup_en=GPIO_PULLUP_ENABLE,
        .scl_pullup_en=GPIO_PULLUP_ENABLE,
        .master={.clk_speed=Frequency},
        //.clk_flags=I2C_SCLK_SRC_FLAG_FOR_NOMAL,
    }; 

    //https://github.com/espressif/esp-idf/blob/5faf116d26/components/esp_hw_support/include/esp_intr_alloc.h
    ESP_ERROR_CHECK(i2c_param_config(I2cNum, 
    &i2c_config));

    ESP_ERROR_CHECK(i2c_driver_install(I2cNum, 
    i2c_config.mode, 
    0, 0, 0)); 

    ESP_LOGI(LOG_TAG, "I2C Init success."); 

    return ESP_OK;
}

esp_err_t i2cDrv_write(uint8_t slave_addr, uint8_t* buffer_ptr, uint32_t buffer_size, uint8_t I2cNum)
{
    return i2c_master_write_to_device(I2cNum, slave_addr, buffer_ptr, buffer_size, portMAX_DELAY);
}

