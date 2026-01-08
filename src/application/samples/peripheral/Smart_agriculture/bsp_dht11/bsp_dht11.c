/**
 ****************************************************************************************************
 * @file        bsp_dht11.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-14
 * @brief       DHT11温湿度传感器实验
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

#include "bsp_dht11.h"

// DHT11输出模式
void dht11_io_out(void)
{
    uapi_pin_set_mode(DHT11_PIN, HAL_PIO_FUNC_GPIO);
    uapi_pin_set_pull(DHT11_PIN, PIN_PULL_TYPE_UP); // 设置为上拉模式
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
}

// DHT11输入模式
void dht11_io_in(void)
{
    uapi_pin_set_pull(DHT11_PIN, PIN_PULL_TYPE_DISABLE); // 设置为浮空模式
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_INPUT); // 输入模式
}

/******************************************************
 * 函数名   ：GPIO_GetInputValue
 * 功能     ：获取GPIO输入状态
 * 输入     ：id, *val
 * 输出     ：0/1
 *******************************************************/
gpio_level_t DHT11_DQ_IN = {0};
uint8_t GPIO_GetInputValue(gpio_level_t *val)
{
    DHT11_DQ_IN = uapi_gpio_get_val(DHT11_PIN);
    return *val;
}

// DHT11复位
void dht11_reset(void)
{
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    DHT11_DQ_OUT(0); //拉低DQ
    uapi_tcxo_delay_ms(20); // 最低拉低18ms
    DHT11_DQ_OUT(1); //拉高DQ
    uapi_tcxo_delay_us(30); // 30us
}

//等待DHT11的回应
//返回1:未检测到DHT11的存在
//返回0:存在
uint8_t dht11_check(void) 
{
    uint8_t retry = 0;
    dht11_io_in();
    // DHT11会拉低40-50us
    while(GPIO_GetInputValue(&DHT11_DQ_IN) && retry < 100)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    }

    if(retry>=100)return 1;
	else retry=0;

    //DHT11拉低后会再次拉高40~50us
    while ((!GPIO_GetInputValue(&DHT11_DQ_IN))&&retry<100)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    }

    if(retry>=100)return 1;	    
	return 0;
}

/*******************************************************************************
* 函 数 名         : DHT11_read_bit
* 函数功能		   : 从DHT11读取一个位
* 输    入         : 无
* 输    出         : 1/0
*******************************************************************************/
uint8_t dht11_read_bit(void) 	
{
    uint8_t retry=0;
    dht11_io_in();
    // 等待变为低电平，12-14us开始
    while(GPIO_GetInputValue(&DHT11_DQ_IN) && retry < 100)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    }
    retry=0;
    //等待变为高电平
    while(!GPIO_GetInputValue(&DHT11_DQ_IN) && retry < 100)
    {
        retry++;
        uapi_tcxo_delay_us(1);
    }
    // 等待40us
    uapi_tcxo_delay_us(40);
    if(GPIO_GetInputValue(&DHT11_DQ_IN)) return 1;
    else return 0;
}

/*******************************************************************************
* 函 数 名         : DHT11_read_byte
* 函数功能		   : 从DHT11读取一个字节
* 输    入         : 无
* 输    出         : 一个字节数据
*******************************************************************************/
uint8_t dht11_read_byte(void)    // read one byte
{
    uint8_t i,dat;
    dat=0;
	for (i=0;i<8;i++) 
	{
   		dat<<=1; 
	    dat|=dht11_read_bit();
    }						    
    return dat;
}

/*******************************************************************************
* 函 数 名         : dht11_read_data
* 函数功能		   : 从DHT11读取温湿度数据
* 输    入         : 温度，湿度
* 输    出         : 0,正常;1,读取失败
*******************************************************************************/
uint8_t dht11_read_data(uint8_t *temp,uint8_t *humi)    
{        
 	uint8_t buf[5];
	uint8_t i;
	dht11_reset();
	if(dht11_check()==0)
	{
		for(i=0;i<5;i++)//读取40位数据
		{
			buf[i]=dht11_read_byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])
		{
			*humi=buf[0];
			*temp=buf[2];
		}
		
	}else return 1;
	return 0;	    
}

//DHT11初始化 
//返回0：初始化成功，1：失败
uint8_t dht11_init(void)
{
    uapi_pin_set_mode(DHT11_PIN, PIN_MODE_0);
    uapi_pin_set_pull(DHT11_PIN, PIN_PULL_TYPE_UP); // 设置为上拉模式
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    
    dht11_reset();
	return dht11_check();
}