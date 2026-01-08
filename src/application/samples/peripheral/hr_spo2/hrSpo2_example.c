#include <stdio.h>
#include "string.h"
#include "cmsis_os2.h"

#include "max30102.h"

#define HRSPO2_TASK_PRIO              24
#define HRSPO2_TASK_STACK_SIZE        0x1000

// static void ShowHrSpO2DataTask(void)
// {
//     Max30102Init();
//     StartMax30102Task();
//     char str[128] = {0};
//     uint8_t heartRate = 0;
//     uint8_t spO2 = 0;
//     while (1)
//     {
//         heartRate = Max30102GetHeartRate();
//         spO2 = Max30102GetSpO2();
//         sprintf(str, "HR=%d", heartRate);

//         sprintf(str, "SpO2=%d", spO2);

//         sleep(1);
//     }
// }

static void HrSpo2_entry(void)
{
    printf("\n HrSpo2 \n");
    // osal_kthread_lock();
    // int ret = StartMax30102Task();
    // if (-1 == ret) 
    // {
    //     printf("[HrSpO3Demo] Falied to create max30102 Task!\n");
    // }
    // else
    // {
    //     printf("[HrSpO3Demo] Success to create max30102 Task!\n");
    // }

    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)Max30102Task, 0, "Max30102Task", HRSPO2_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, HRSPO2_TASK_PRIO);
    }
    osal_kthread_unlock();

}

app_run(HrSpo2_entry);