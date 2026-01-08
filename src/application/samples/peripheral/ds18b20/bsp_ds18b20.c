/**
 ****************************************************************************************************
 * @file        bsp_ds18b20.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-08
 * @brief       DS18B20温度传感器实验
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 */

#include "bsp_ds18b20.h"

// 输出配置
void ds18b20_io_out(void)
{
    uapi_pin_set_mode(DS18B20_PIN, HAL_PIO_FUNC_GPIO); // GPIO模式
    uapi_gpio_set_dir(DS18B20_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    uapi_pin_set_pull(DS18B20_PIN, PIN_PULL_TYPE_UP); // 设置为上拉模式
}

// 输入配置
void ds18b20_io_in(void)
{
    uapi_pin_set_pull(DS18B20_PIN, PIN_PULL_TYPE_DISABLE); // 设置为浮空模式
    uapi_gpio_set_dir(DS18B20_PIN, GPIO_DIRECTION_INPUT); // 输入模式
}

/******************************************************
 * 函数名   ：GPIO_GetInputValue
 * 功能     ：获取GPIO输入状态
 * 输入     ：*val
 * 输出     ：0/1
 *******************************************************/
gpio_level_t DS18B20_DQ_IN = {0};
uint8_t GPIO_GetInputValue(gpio_level_t *val)
{
    DS18B20_DQ_IN = uapi_gpio_get_val(DS18B20_PIN);
    return *val;
}

// ds18b20复位
void ds18b20_reset(void)
{
    uapi_gpio_set_dir(DS18B20_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    DS18B20_DQ_OUT(0);          //拉低DQ
	uapi_tcxo_delay_us(750);    //拉低750us
	DS18B20_DQ_OUT(1);          //DQ=1 
	uapi_tcxo_delay_us(15);     //15US

}

/*******************************************************************************
* 函 数 名         : ds18b20_check
* 函数功能		   : 检测DS18B20是否存在
* 输    入         : 无
* 输    出         : 1:未检测到DS18B20的存在，0:存在
*******************************************************************************/
uint8_t ds18b20_check(void) 
{
    uint8_t retry=0;
	ds18b20_io_in(); 
    while(GPIO_GetInputValue(&DS18B20_DQ_IN) && retry<200)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    };

    if(retry >= 200)return 1;
    else retry = 0;
    while ((!GPIO_GetInputValue(&DS18B20_DQ_IN))&&retry<240)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    };

    if(retry>=240)return 1;
  
	return 0;

}

/*******************************************************************************
* 函 数 名         : ds18b20_read_bit
* 函数功能		   : 从DS18B20读取一个位
* 输    入         : 无
* 输    出         : 1/0
*******************************************************************************/
uint8_t ds18b20_read_bit(void) 			 // read one bit
{
    uint8_t data;
    
    uapi_gpio_set_dir(DS18B20_PIN,GPIO_DIRECTION_OUTPUT);
    DS18B20_DQ_OUT(0); 
    uapi_tcxo_delay_us(2);
    DS18B20_DQ_OUT(1); 
    ds18b20_io_in();
    uapi_tcxo_delay_us(12);
    if(GPIO_GetInputValue(&DS18B20_DQ_IN))data=1;
	else data=0;	 
	uapi_tcxo_delay_us(50);           
	return data;
}

/*******************************************************************************
* 函 数 名         : ds18b20_read_byte
* 函数功能		   : 从DS18B20读取一个字节
* 输    入         : 无
* 输    出         : 一个字节数据
*******************************************************************************/
uint8_t ds18b20_read_byte(void)    // read one byte
{
    uint8_t i,j,dat;
    dat=0;
	for (i=0;i<8;i++) 
	{
		j=ds18b20_read_bit();
        dat=(j<<7)|(dat>>1);
    }						    
    return dat;
}

/*******************************************************************************
* 函 数 名         : ds18b20_write_byte
* 函数功能		   : 写一个字节到DS18B20
* 输    入         : dat：要写入的字节
* 输    出         : 无
*******************************************************************************/
void ds18b20_write_byte(uint8_t dat)  
{
    uint8_t j;
    uint8_t testb;
	hal_gpio_setdir(DS18B20_PIN, GPIO_DIRECTION_OUTPUT);
    for (j=1;j<=8;j++) 
	{
        testb=dat&0x01;
        dat=dat>>1;
        if (testb) 
        {
            DS18B20_DQ_OUT(0);// Write 1
            uapi_tcxo_delay_us(2);                            
            DS18B20_DQ_OUT(1);
            uapi_tcxo_delay_us(60);             
        }
        else 
        {
            DS18B20_DQ_OUT(0);// Write 0
            uapi_tcxo_delay_us(60);             
            DS18B20_DQ_OUT(1);
            uapi_tcxo_delay_us(2);                          
        }
    }
}   

/*******************************************************************************
* 函 数 名         : ds18b20_start
* 函数功能		   : 开始温度转换
* 输    入         : 无
* 输    出         : 无
*******************************************************************************/
void ds18b20_start(void)// ds1820 start convert
{
    ds18b20_reset();	   
	ds18b20_check();	 
    ds18b20_write_byte(0xcc);// skip rom
    ds18b20_write_byte(0x44);// convert
}

//DS18B20初始化
uint8_t ds18b20_init(void)
{
    uapi_pin_set_mode(DS18B20_PIN, HAL_PIO_FUNC_GPIO);
    uapi_pin_set_pull(DS18B20_PIN, PIN_PULL_TYPE_UP); // 设置为上拉模式
    uapi_gpio_set_dir(DS18B20_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    
    ds18b20_reset();
	return ds18b20_check();
}

/*******************************************************************************
* 函 数 名         : ds18b20_gettemperture
* 函数功能		   : 从ds18b20得到温度值
* 输    入         : 无
* 输    出         : 温度数据
*******************************************************************************/ 
uint16_t ds18b20_gettemperture(void)
{
    uint16_t temp;
	uint8_t LSB,MSB;

    ds18b20_start();                    // ds1820 start convert
    ds18b20_reset();
    ds18b20_check();	

    ds18b20_write_byte(0xcc);           // skip rom
    ds18b20_write_byte(0xbe);           // convert	    
    
    LSB=ds18b20_read_byte();              // LSB   
    MSB=ds18b20_read_byte();              // MSB   
	
    int falg = MSB & 0x80; // 获取符号
    if(falg == 0x80) // 负数高位为1
    {
        temp = MSB << 8; // 将低8位移动到高8位
        temp = temp | LSB; // 将高低温数据合并
        temp = ~temp+1; // 取反加一获取原码，符号位不参与运算
        temp = temp * 0.98;//12位精度为0.98

        return temp;
    }
	else // 正数高位为0
    {
        temp = MSB << 8;
        temp = temp | LSB;
        temp = (temp >> 4) & 0x00ff; //温度数据中低4位没有用
        temp = temp * 0.98;//12位精度为0.98

        return temp;
    }

}