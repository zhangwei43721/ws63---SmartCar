#include "../bsp_include/bsp_beep.h"

void beep_init(void)
{
    printf("beep init...\n");
    uapi_pin_set_mode(BEEP_PIN, HAL_PIO_FUNC_GPIO); // gpio默认模式
    uapi_gpio_set_dir(BEEP_PIN, GPIO_DIRECTION_OUTPUT); // 输出模式
    //uapi_gpio_set_val(BEEP_PIN, GPIO_LEVEL_HIGH);
}

void beep_alarm(uint16_t cnt, uint16_t time)
{
    while(cnt--)
    {
        BEEP(1);
        uapi_tcxo_delay_us(time);
        BEEP(0);
        uapi_tcxo_delay_us(time);
    }
}