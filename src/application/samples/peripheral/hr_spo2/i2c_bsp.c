#include <stdbool.h>
#include "soc_osal.h"
#include "securec.h"
#include "common_def.h"
#include "tcxo.h"
#include "i2c.h"
#include "gpio.h"
#include "pinctrl.h"
#include "i2c_bsp.h"
#include "cmsis_os2.h"

#define MAX_I2C_NUM     2
osMutexId_t gArrayMutexId[MAX_I2C_NUM] = {NULL, NULL};
#define MUTEX_TIMEOUT   100 

uint32_t I2cBspInit(uint32_t id, uint32_t baudrate)
{
    if(id > MAX_I2C_NUM) { return ERRCODE_I2C_INVALID_PARAMETER; }

     if (gArrayMutexId[id] != NULL)
    {
        osMutexAttr_t attr = {0};
        gArrayMutexId[id] = osMutexNew(&attr);  
    }//

    return uapi_i2c_master_init(id, baudrate, 0x0);
}

uint32_t I2cBspDeinit(uint32_t id)
{
    if(id > MAX_I2C_NUM) { return ERRCODE_I2C_INVALID_PARAMETER; }

    if (gArrayMutexId[id] != NULL) { osMutexDelete(gArrayMutexId[id]); }
    
    return 0;
}

uint32_t I2cBspWrite(uint32_t id, uint16_t deviceAddr, uint8_t *data, uint32_t dataLen)
{
    if(id > MAX_I2C_NUM) { return ERRCODE_I2C_INVALID_PARAMETER; }

    osMutexAcquire(gArrayMutexId[id], osWaitForever);

    i2c_data_t i2c_data = {0};
    i2c_data.send_buf = data;
    i2c_data.send_len = dataLen;

    uint32_t ret = uapi_i2c_master_write((i2c_bus_t)id, deviceAddr, &i2c_data);
    osMutexRelease(gArrayMutexId[id]);

    return ret;
}

uint32_t I2cBspRead(uint32_t id, uint16_t deviceAddr, uint8_t *data, uint32_t dataLen)
{
    if(id > MAX_I2C_NUM) { return ERRCODE_I2C_INVALID_PARAMETER; }

    osMutexAcquire(gArrayMutexId[id], osWaitForever);

    i2c_data_t i2c_data = { 0 };
    i2c_data.receive_buf = (uint8_t *)data;
    i2c_data.receive_len = (uint32_t)dataLen;
    uint32_t ret = uapi_i2c_master_read((i2c_bus_t)id, deviceAddr, &i2c_data);

    osMutexRelease(gArrayMutexId[id]);

    return ret;
}

uint32_t I2cBspSetBaudrate(uint32_t id, uint32_t baudrate)
{
    return uapi_i2c_set_baudrate(id, baudrate);
}