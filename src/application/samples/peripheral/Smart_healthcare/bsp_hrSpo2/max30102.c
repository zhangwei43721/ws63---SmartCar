#include "max30102.h"
#include "uart.h"
#include "i2c_ex.h"
//#include "i2c_bsp.h"
#include "max30102_i2c_bus.h"

#define MAX30102_TASK_STACK_SIZE     4096             //任务栈大小
#define MAX30102_TASK_PRIO           10                 //任务优先等级
#define MAX30102_I2C_IDX             1                  //I2C设备号
#define MAX30102_I2C_BAUDRATE        (200*1000)         //I2C波特率
#define MAX30102_I2C_SDA             13                 //I2C1 SDA对应引脚
#define MAX30102_I2C_SCL             14                 //I2C1 SCL对应引脚
#define MAX30102_ADDR                0x57               //MAX30102设备地址

#define MAX30102_INT_GPIO           0                  //MAX30102中断引脚
#define MAX_BRIGHTNESS              255     



static bool finger = false;  
#define DEF_TIMEOUT     (5*10000)                      //超时时间5s

static osThreadId_t g_tid = NULL;

//register addresses
#define REG_INTR_STATUS_1           0x00
#define REG_INTR_STATUS_2           0x01
#define REG_INTR_ENABLE_1           0x02
#define REG_INTR_ENABLE_2           0x03
#define REG_FIFO_WR_PTR             0x04
#define REG_OVF_COUNTER             0x05
#define REG_FIFO_RD_PTR             0x06
#define REG_FIFO_DATA               0x07
#define REG_FIFO_CONFIG             0x08
#define REG_MODE_CONFIG             0x09
#define REG_SPO2_CONFIG             0x0A
#define REG_LED1_PA                 0x0C
#define REG_LED2_PA                 0x0D
#define REG_PILOT_PA                0x10
#define REG_MULTI_LED_CTRL1         0x11
#define REG_MULTI_LED_CTRL2         0x12
#define REG_TEMP_INTR               0x1F
#define REG_TEMP_FRAC               0x20
#define REG_TEMP_CONFIG             0x21
#define REG_PROX_INT_THRESH         0x30
#define REG_REV_ID                  0xFE
#define REG_PART_ID                 0xFF


uint32_t arrIrBuf[500] = {0};     //IR LED sensor data
uint32_t arrRedBuf[500] = {0};    //Red LED sensor data

uint32_t un_min, un_max, un_prev_data;
int32_t n_ir_buffer_length;    //数据长度
uint8_t temp[6];
uint32_t aun_red_buffer[500];  //Red LED	红光数据，用于计算心率曲线以及计算心率
uint32_t aun_ir_buffer[500]; 	 //IR LED   红外光数据，用于计算血氧

int32_t n_sp02; //SPO2值
int8_t ch_spo2_valid;   //用于显示SP02计算是否有效的指示符

int32_t n_heart_rate;   //心率值
int8_t  ch_hr_valid;    //用于显示心率计算是否有效的指示符

uint8_t str[100];
uint8_t dis_hr=0,dis_spo2=0;

int i;
float f_temp;
int32_t n_brightness;

/*
* 函数名称 : Max30102WriteReg
* 功能描述 : max30102写寄存器
* 参    数 : addr   - 寄存器地址
             data   - 数据内容
* 返回值   : 成功返回0,失败返回-1
* 示    例 : Max30102WriteReg(addr,data);
*/
// int Max30102WriteReg(uint8_t addr, uint8_t data)
// {
//     uint8_t buffer[2]={addr, data};
//     uint32_t ret = I2cBspWrite(MAX30102_I2C_IDX, (MAX30102_ADDR<<1)|0,buffer, array_size(buffer));
//     if (ret != ERRCODE_SUCC)
//     {
//         I2cReset();
//         printf("max30102 write failed, %0x\n",ret);
//         return -1;
//     }//

//     return 0;
// }





// int Max30102ReadReg(uint8_t addr, uint8_t *pData, uint16_t len)
// {
//     if(NULL == pData) {return -1;}
    
//     uint32_t ret = I2cBspWrite(MAX30102_I2C_IDX,(MAX30102_ADDR<<1)|0, &addr, 1);
//     if (ret != 0 ) { I2cReset(); return -1; }
    
//     ret = I2cBspRead(MAX30102_I2C_IDX,(MAX30102_ADDR<<1)|1, pData, len);
//     if(ret != 0)
//     {
//         I2cReset();
//         printf("max30102 read failed,%0x\n",ret);
//         return -1;
//     }
//     return 0;
// }

// 读取gpio_10
uint8_t max30102_ReadInputStatus(void)
{
    gpio_direction_t val = 0;
    uapi_pin_set_mode(MAX30102_INT_GPIO, HAL_PIO_FUNC_GPIO); // 设置IO为GPIO功能
    uapi_gpio_set_dir(MAX30102_INT_GPIO, GPIO_DIRECTION_INPUT); // 输入模式
    uapi_pin_set_pull(MAX30102_INT_GPIO, PIN_PULL_TYPE_DISABLE);

    val = uapi_gpio_get_val(MAX30102_INT_GPIO);

    return val;
}

// 设置为输入模式
void Max30102SetInput(void)
{
    uapi_pin_set_mode(MAX30102_INT_GPIO, HAL_PIO_FUNC_GPIO); // 设置IO为GPIO功能
    uapi_gpio_set_dir(MAX30102_INT_GPIO, GPIO_DIRECTION_INPUT); // 输入模式
}

// 初始化Max30102
void Max30102Init(void)
{
    max30102_i2c_init();
    Max30102Reset();

    max30102_Bus_Write(REG_INTR_ENABLE_1,0xc0);	// INTR setting
	max30102_Bus_Write(REG_INTR_ENABLE_2,0x00);
	max30102_Bus_Write(REG_FIFO_WR_PTR,0x00);  	//FIFO_WR_PTR[4:0]
	max30102_Bus_Write(REG_OVF_COUNTER,0x00);  	//OVF_COUNTER[4:0]
	max30102_Bus_Write(REG_FIFO_RD_PTR,0x00);  	//FIFO_RD_PTR[4:0]
	max30102_Bus_Write(REG_FIFO_CONFIG,0x0f);  	//sample avg = 1, fifo rollover=false, fifo almost full = 17
	max30102_Bus_Write(REG_MODE_CONFIG,0x03);  	//0x02 for Red only, 0x03 for SpO2 mode 0x07 multimode LED
	max30102_Bus_Write(REG_SPO2_CONFIG,0x27);  	// SPO2_ADC range = 4096nA, SPO2 sample rate (100 Hz), LED pulseWidth (400uS)  
	max30102_Bus_Write(REG_LED1_PA,0x24);   	//Choose value for ~ 7mA for LED1
	max30102_Bus_Write(REG_LED2_PA,0x24);   	// Choose value for ~ 7mA for LED2
	max30102_Bus_Write(REG_PILOT_PA,0x7f);   	// Choose value for ~ 25mA for Pilot LED			

}

// 复位
void Max30102Reset(void)
{
    max30102_Bus_Write(REG_MODE_CONFIG,0x40);
    max30102_Bus_Write(REG_MODE_CONFIG,0x40);
}

/*
* 函数名称 : Max30102FifoReadBytes
* 功能描述 : max30102 fifo数据读取
* 参    数 
             Data - 数据指针
* 返回值   : 无
* 示    例 : Max30102FifoReadBytes();
*/
// int Max30102FifoReadBytes(uint8_t *pData)
// {	
//     uint8_t temp;
//     //read and clear status register
//     int ret = Max30102ReadReg(REG_INTR_STATUS_1, &temp, sizeof(temp));
//     ret += Max30102ReadReg(REG_INTR_STATUS_2, &temp, sizeof(temp));

//     ret += Max30102ReadReg(REG_FIFO_DATA, pData, 96);
//     if (ret)
//     {
//         printf("max30102 read fifo failed.\n");
//     }//

//     return ret;
// }


/*
* 函数名称 : Max30102GetHeartRate
* 功能描述 : 获取心率
* 参    数 : 无
* 返回值   : 心率
* 示    例 : HeartRate = Max30102GetHeartRate();
*/
uint8_t Max30102GetHeartRate(void)
{
    return dis_hr;                        //传递心率值
}

/*
* 函数名称 : Max30102GetSpO2
* 功能描述 : 获取检测温度
* 参    数 : 无
* 返回值   : 血氧饱和度
* 示    例 : s_Spo2 = Max30102GetSpO2();
*/
uint8_t Max30102GetSpO2(void)
{
    return dis_spo2;                      //传递血氧饱和度值
}

bool Max30102GetFingerStatus()
{
    return finger;
}

void FlagClear()
{
    dis_hr=0;
    dis_spo2=0;
    finger=false;
}

/*
* 函数名称 : MAX30102Task
* 功能描述 : MAX30102任务，计算出心率血氧值(使用算法粗糙，心率血氧值不准确，只能演示)
* 参    数 : 无
* 返回值   : 无
* 示    例 : MAX30102Task()_3;
*/

void *Max30102Task(void *arg)
{
    
    unused(arg);

    Max30102Init();

    un_min = 0x3FFFF;
    un_max = 0;
    
    n_ir_buffer_length = 500; //缓冲区长度为100，可存储以100sps运行的5秒样本

    for(i = 0; i < n_ir_buffer_length; i++)
    {
        while (1 == max30102_ReadInputStatus()); // 等待，直到中断引脚断言

        max30102_FIFO_ReadBytes(REG_FIFO_DATA, temp);
        aun_red_buffer[i] =  (long)((long)((long)temp[0]&0x03)<<16) | (long)temp[1]<<8 | (long)temp[2];    // 将值合并得到实际数字
		aun_ir_buffer[i] = (long)((long)((long)temp[3] & 0x03)<<16) |(long)temp[4]<<8 | (long)temp[5];   	 // 将值合并得到实际数字
        
        if(un_min>aun_red_buffer[i])
		    un_min=aun_red_buffer[i];    //更新计算最小值
		if(un_max<aun_red_buffer[i])
			un_max=aun_red_buffer[i];    //更新计算最大值
    }

    un_prev_data=aun_red_buffer[i];

    //计算前500个样本（前5秒的样本）后的心率和血氧饱和度
	maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_sp02, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid); 

    while(1)
    {
        //舍去前100组样本，并将后400组样本移到顶部，将100~500缓存数据移位到0~400
        for(i=100;i<500;i++)
        {
                aun_red_buffer[i-100]=aun_red_buffer[i];	//将100-500缓存数据移位到0-400
                aun_ir_buffer[i-100]=aun_ir_buffer[i];		//将100-500缓存数据移位到0-400
                
                //update the signal min and max
                if(un_min>aun_red_buffer[i])			//寻找移位后0-400中的最小值
                un_min=aun_red_buffer[i];
                if(un_max<aun_red_buffer[i])			//寻找移位后0-400中的最大值
                un_max=aun_red_buffer[i];
        }

        //在计算心率前取100组样本，取的数据放在400-500缓存数组中 
        for(i = 400; i < 500; i++)
        {
            un_prev_data=aun_red_buffer[i-1];	//在计算心率前取100组样本，取的数据放在400-500缓存数组中
			while(1 == uapi_gpio_get_val(MAX30102_INT_GPIO));
            //读取传感器数据，赋值到temp中
            max30102_FIFO_ReadBytes(REG_FIFO_DATA,temp); 
            //将值合并得到实际数字，数组400-500为新读取数据
            aun_red_buffer[i] =  (long)((long)((long)temp[0]&0x03)<<16) | (long)temp[1]<<8 | (long)temp[2];
            //将值合并得到实际数字，数组400-500为新读取数据
			aun_ir_buffer[i] = (long)((long)((long)temp[3] & 0x03)<<16) |(long)temp[4]<<8 | (long)temp[5];
            if(aun_red_buffer[i]>un_prev_data)		//用新获取的一个数值与上一个数值对比
            {
                f_temp=aun_red_buffer[i]-un_prev_data;
                f_temp/=(un_max-un_min);
                f_temp*=MAX_BRIGHTNESS;	   //公式（心率曲线）=（新数值-旧数值）/（最大值-最小值）*255
                n_brightness-=(int)f_temp;
                if(n_brightness<0)
                    n_brightness=0;
            }
            else
            {
                f_temp=un_prev_data-aun_red_buffer[i];
                f_temp/=(un_max-un_min);
                f_temp*=MAX_BRIGHTNESS;			//公式（心率曲线）=（旧数值-新数值）/（最大值-最小值）*255
                n_brightness+=(int)f_temp;
                if(n_brightness>MAX_BRIGHTNESS)
                    n_brightness=MAX_BRIGHTNESS;
            }

            // 计算结果
            if(ch_hr_valid == 1 && n_heart_rate<120)
            {
                dis_hr = n_heart_rate;
				dis_spo2 = n_sp02;
            }
            else
            {
                dis_hr = 0;
                dis_spo2 = 0;
            }

        }

        maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_sp02, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);

        printf("心率= %d BPM 血氧= %d\r\n ",dis_hr,dis_spo2);
        osal_msleep(10);
        //uapi_watchdog_kick(); 
    }

}

/*
* 函数名称 : StartMax30102Task
* 功能描述 : 启动Max30102任务
* 参    数 : 空
* 返回值   : 空
* 示    例 : StartMax30102Task();
*/
int StartMax30102Task(void)
{
    // osal_task *task_handle = NULL;
    
    // osal_kthread_lock();
    // task_handle = osal_kthread_create((osal_kthread_handler)Max30102Task, 0, "Max30102Task", MAX30102_TASK_STACK_SIZE);
    // if (task_handle != NULL) {
    //     osal_kthread_set_priority(task_handle, MAX30102_TASK_PRIO);
    //     osal_kfree(task_handle);
    // }
    // osal_kthread_unlock();

    //Max30102Task(NULL);

    return 0;
}

void StopMax30102Task(void)
{
    if(NULL == g_tid) { return; }

    osThreadState_t tidStatus = osThreadInactive;
    

    tidStatus = osThreadGetState(g_tid);
    if (tidStatus<=osThreadBlocked && tidStatus>osThreadInactive)
    {
        osThreadTerminate(g_tid);
        //printf("kill g_tid :%d, g_tid_status is %d\r\n", g_tid, g_tid_status);
        osal_msleep(200);
        //printf("g_tid = NULL :%d, g_tid_status is %d\r\n", g_tid, g_tid_status);
        g_tid = NULL;
        FlagClear();
    }
    
}
