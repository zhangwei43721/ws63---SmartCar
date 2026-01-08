/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2021-2022. All rights reserved.
 * Description: semaphore
 *
 * Create: 2021-12-16
 */

#include <los_sem.h>
#include <los_memory.h>
#include <los_sem_pri.h>
#include "soc_osal.h"
#include "osal_errno.h"
#include "osal_inner.h"

int osal_sem_init(osal_semaphore *sem, int val)
{
    if (sem == NULL || val < 0) {
        osal_log("val=%d parameter invalid!\n", val);
        return OSAL_FAILURE;
    }
    unsigned int ret = LOS_SemCreate(val, (unsigned int *)&(sem->sem)); /* semid may be zero */
    if (ret != LOS_OK) {
        osal_log("LOS_SemCreate failed! ret = %#x.\n", ret);
        return OSAL_FAILURE;
    }
    return OSAL_SUCCESS;
}

int osal_sem_binary_sem_init(osal_semaphore *sem, int val)
{
    if (sem == NULL || val < 0 || val > OS_SEM_BINARY_COUNT_MAX) {
        osal_log("val=%d parameter invalid!\n", val);
        return OSAL_FAILURE;
    }
    unsigned int ret = LOS_BinarySemCreate(val, (unsigned int *)&(sem->sem));
    if (ret != OSAL_SUCCESS) {
        osal_log("LOS_BinarySemCreate failed! ret = %#x.\n", ret);
        return OSAL_FAILURE;
    }
    return OSAL_SUCCESS;
}

void osal_sem_destroy(osal_semaphore *sem)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return;
    }
    unsigned int ret = LOS_SemDelete((unsigned int)(UINTPTR)sem->sem);
    if (ret != LOS_OK) {
        osal_log("LOS_SemDelete failed! ret = %#x.\n", ret);
    }
}

int osal_sem_down(osal_semaphore *sem)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return OSAL_FAILURE;
    }
    unsigned int ret = LOS_SemPend((unsigned int)(UINTPTR)sem->sem, LOS_WAIT_FOREVER);
    if (ret == LOS_ERRNO_SEM_TIMEOUT) {
        osal_log("OSAL_SEM_WAIT_TIME_OUT!\n");
        return OSAL_FAILURE;
    } else if (ret != OSAL_SUCCESS) {
        osal_log("LOS_SemPend failed! ret = %#x.\n", ret);
        return OSAL_FAILURE;
    } else {
        return OSAL_SUCCESS;
    }
}

int osal_sem_down_timeout(osal_semaphore *sem, unsigned int timeout)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return OSAL_FAILURE;
    }
    unsigned int ticks = (timeout == LOS_WAIT_FOREVER) ? timeout : LOS_MS2Tick(timeout);

    unsigned int ret = LOS_SemPend((unsigned int)(UINTPTR)sem->sem, ticks);
    if (ret == LOS_ERRNO_SEM_TIMEOUT) {
        osal_log("LOS_ERRNO_SEM_TIMEOUT!\n");
        return OSAL_ETIME;
    } else if (ret != OSAL_SUCCESS) {
        osal_log("LOS_SemPend failed! ret = %#x.\n", ret);
        return OSAL_FAILURE;
    } else {
        return OSAL_SUCCESS;
    }
}

int osal_sem_down_interruptible(osal_semaphore *sem)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return OSAL_FAILURE;
    }

    unsigned int ret = LOS_SemPend((unsigned int)(UINTPTR)sem->sem, LOS_WAIT_FOREVER);
    if (ret != OSAL_SUCCESS) {
        osal_log("LOS_SemPend failed! ret = %#x.\n", ret);
        return OSAL_FAILURE;
    } else {
        return OSAL_SUCCESS;
    }
}

int osal_sem_trydown(osal_semaphore *sem)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return 1;
    }

    unsigned int ret = LOS_SemPend((unsigned int)(UINTPTR)sem->sem, 0);
    if (ret == LOS_ERRNO_SEM_PEND_IN_LOCK) {
        osal_log("LOS_ERRNO_SEM_PEND_IN_LOCK!\n");
        return 1;
    } else if (ret != LOS_OK) {
        osal_log("LOS_SemPend failed! ret = %#x.\n", ret);
        return 1;
    } else {
        return OSAL_SUCCESS;
    }
}

void osal_sem_up(osal_semaphore *sem)
{
    if (sem == NULL) {
        osal_log("parameter invalid!\n");
        return;
    }

    unsigned int ret = LOS_SemPost((unsigned int)(UINTPTR)sem->sem);
    if (ret != LOS_OK) {
        osal_log("LOS_SemPost failed! ret = %#x.\n", ret);
    }
}

/* 
 * 新增功能 ：获取当前信号量的令牌计数。--异常
 * 参数：semaphore_id：由osSemaphoreNew返回的信号量句柄。
 * 返回值：当前的信号量令牌计数。
 **/

unsigned short int osal_Semaphore_getCount(osal_semaphore *semaphore_id)
{
    UINT32 uwIntSave;
    UINT16 uwCount;

    if (OS_INT_ACTIVE) {
        return 0;
    }

    if (semaphore_id->sem == NULL) {
        return 0;
    }

    uwIntSave = LOS_IntLock();
    //uwCount = ((LosSemCB *)semaphore_id->sem)->semCount;

    LosSemCB *semCB = semaphore_id->sem;
    uwCount = semCB->semCount;
    LOS_IntRestore(uwIntSave);

    return uwCount;
}
