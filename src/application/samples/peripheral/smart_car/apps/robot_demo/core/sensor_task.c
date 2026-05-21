#include "sensor_task.h"

#include <stdio.h>

#include "../../../drivers/hcsr04/bsp_hcsr04.h"
#include "robot_mgr.h"
#include "soc_osal.h"

#define SENSOR_TASK_INTERVAL_MS 50
#define SENSOR_TASK_STACK_SIZE 2048
#define SENSOR_TASK_PRIO 23

static osal_task *g_sensor_task = NULL;

static int sensor_task_entry(void *arg)
{
    (void)arg;

    printf("[Sensor] 采集任务启动\r\n");

    while (1) {
        float dist = hcsr04_get_distance();
        robot_mgr_update_distance(dist);
        osal_msleep(SENSOR_TASK_INTERVAL_MS);
    }
    return 0;
}

void sensor_task_init(void)
{
    if (g_sensor_task != NULL)
        return;

    osal_kthread_lock();
    g_sensor_task =
        osal_kthread_create((osal_kthread_handler)sensor_task_entry, NULL, "sensor_task", SENSOR_TASK_STACK_SIZE);
    if (g_sensor_task != NULL)
        osal_kthread_set_priority(g_sensor_task, SENSOR_TASK_PRIO);

    osal_kthread_unlock();

    if (g_sensor_task != NULL)
        printf("[Sensor] 采集任务初始化完成\r\n");
    else
        printf("[Sensor] 采集任务创建失败\r\n");
}
