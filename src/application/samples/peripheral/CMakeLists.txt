#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
if(DEFINED CONFIG_SAMPLE_SUPPORT_AMIC)
    add_subdirectory_if_exist(amic)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_BLINKY)
    add_subdirectory_if_exist(blinky)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_BUTTON)
    add_subdirectory_if_exist(button)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_TASKS)
    add_subdirectory_if_exist(tasks)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_TCXO)
    add_subdirectory_if_exist(tcxo)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_DMA)
    add_subdirectory_if_exist(dma)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_I2C)
    add_subdirectory_if_exist(i2c)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_I2S)
    add_subdirectory_if_exist(i2s)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_KEYSCAN)
    add_subdirectory_if_exist(keyscan)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_PDM)
    add_subdirectory_if_exist(pdm)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_PDM_AMIC)
    add_subdirectory_if_exist(pdm_amic)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_PINCTRL)
    add_subdirectory_if_exist(pinctrl)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_QDEC)
    add_subdirectory_if_exist(qdec)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_SFC)
    add_subdirectory_if_exist(sfc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_SPI)
    add_subdirectory_if_exist(spi)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_SYSTICK)
    add_subdirectory_if_exist(systick)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WDT)
    add_subdirectory_if_exist(watchdog)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_PWM)
    add_subdirectory_if_exist(pwm)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_UART)
    add_subdirectory_if_exist(uart)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_ADC)
    add_subdirectory_if_exist(adc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_RTC)
    add_subdirectory_if_exist(rtc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_TIMER)
    add_subdirectory_if_exist(timer)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_EFLASH)
    add_subdirectory_if_exist(eflash)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_CALENDAR)
    add_subdirectory_if_exist(calendar)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_I2S_DMA)
    add_subdirectory_if_exist(i2s_dma)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_I2S_DMA_LLI)
    add_subdirectory_if_exist(i2s_dma_lli)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_CAN)
    add_subdirectory_if_exist(can)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_CLOCKS)
    add_subdirectory_if_exist(clocks)
endif()

# 添加事件
if(DEFINED CONFIG_SAMPLE_SUPPORT_EVENT)
    add_subdirectory_if_exist(event)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)

# 添加线程子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_THREAD)
    add_subdirectory_if_exist(thread)
endif()

# 添加消息队列子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_MESSAGE)
    add_subdirectory_if_exist(message)
endif()

# 添加信号量子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SEMAPHORE)
    add_subdirectory_if_exist(semaphore)
endif()

# 添加互斥锁子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_MUTEX)
    add_subdirectory_if_exist(mutex)
endif()

# 添加LED子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_LED)
    add_subdirectory_if_exist(led)
endif()

# 添加BEEP子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_BEEP)
    add_subdirectory_if_exist(beep)
endif()

# 添加BUTTON子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_BUTTON)
    add_subdirectory_if_exist(beep)
endif()

# 添加直流电机子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_DC_MOTOR)
    add_subdirectory_if_exist(dc_motor)
endif()

# 添加步进电机子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_STEP_MOTOR)
    add_subdirectory_if_exist(step_motor)
endif()

# 添加外部中断子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_EXTI)
    add_subdirectory_if_exist(exti)
endif()

# 添加外部定时器子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_MYTIMER)
    add_subdirectory_if_exist(MyTimer)
endif()

# 添加pwm led子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_PWM_LED)
    add_subdirectory_if_exist(pwm_led)
endif()

# 添加uart子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_UARTDEMO)
    add_subdirectory_if_exist(uartDemo)
endif()

# 添加ds18b20子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_DS18B20)
    add_subdirectory_if_exist(ds18b20)
endif()

# 添加oled子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_OLED)
    add_subdirectory_if_exist(oled)
endif()

# 添加wifiap子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFIAP)
    add_subdirectory_if_exist(wifiap)
endif()

# 添加wifista子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFISTA)
    add_subdirectory_if_exist(wifista)
endif()

# 添加udp_client子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_UDP_CLIENT)
    add_subdirectory_if_exist(udp_client)
endif()

# 添加tcp_client子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_TCP_CLIENT)
    add_subdirectory_if_exist(tcp_client)
endif()

# 添加wifi_mqtt子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_MQTT)
    add_subdirectory_if_exist(wifi_mqtt)
endif()

# 添加wifi mqtt_huawei_IoTA子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_MQTT_HUAWEI_IOTA)
    add_subdirectory_if_exist(mqtt_huawei_IoTA)
endif()

# 添加温湿度子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_DHT11)
    add_subdirectory_if_exist(dht11)
endif()

# 添加心率血氧子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_HR_SPO2)
    add_subdirectory_if_exist(hr_spo2)
endif()

# 添加人体红外子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_HC_SR501_BODY)
    add_subdirectory_if_exist(HC_SR501_BODY)
endif()

# 添加风扇系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_FAN)
    add_subdirectory_if_exist(fan)
endif()


# 添加智慧健康子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SAMART_HEALTHCARE)
     add_subdirectory_if_exist(Smart_healthcare)
endif()

# 添加土壤湿度子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SOIL_SENSOR)
    add_subdirectory_if_exist(soil_sensor)
endif()

#添加手势识别子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_GESTURE_SENSOR)
    add_subdirectory_if_exist(gesture_sensor)
endif()

#添加语音识别模块子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_VOICE_MODULE)
    add_subdirectory_if_exist(voiceModule)
endif()

#添加智慧农业子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SMART_AGRICULTURE)
    add_subdirectory_if_exist(Smart_agriculture)
endif()

#添加智能家居农业子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SMART_HOME)
    add_subdirectory_if_exist(Smart_home)
endif()

#添加智能手势识别子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_SMART_GESTURE)
    add_subdirectory_if_exist(Smart_gesture)
endif()  

#添加GH719微波感应模块子系统
if(DEFINED CONFIG_SAMPLE_SUPPORT_GH719)
    add_subdirectory_if_exist(GH719)
endif()  

set(SOURCES "${SOURCES}" PARENT_SCOPE)