/**
 * @file        car_utils.c
 * @brief       小车底层 OS 模板与辅助工具实现
 */

#include "car_common.h"
#include <stdio.h>

// 小车内核线程创建助手。封装了标准 4 步模板（lock -> create -> set_priority ->
// unlock），确保锁对称配对并精简业务层创建任务时的样板代码。
osal_task *car_task_create_locked(const char *name,
                                  osal_kthread_handler entry,
                                  void *arg,
                                  unsigned int stack_size,
                                  unsigned int priority)
{
    osal_task *handle = NULL;
    osal_kthread_lock();
    handle = osal_kthread_create(entry, arg, name, stack_size);
    if (handle != NULL) {
        osal_kthread_set_priority(handle, priority);
    }
    osal_kthread_unlock();
    if (handle == NULL) {
        printf("[BUG] task create failed: %s\r\n", name ? name : "(null)");
    }
    return handle;
}

// 消息队列覆写助手（队满时自动读并丢弃最旧数据，再写入最新数据）。
// 三步（查数量 → 丢弃最旧 → 写入）用关中断包成原子段，保证在 ISR 上下文也安全。
int osal_msgq_overwrite(unsigned long qid, unsigned int depth, const void *msg, unsigned int size)
{
    uint32_t irq_sts = osal_irq_lock();
    if (osal_msg_queue_get_msg_num(qid) >= depth) {
        unsigned char drop[64];
        unsigned int dsz = size;
        (void)osal_msg_queue_read_copy(qid, drop, &dsz, OSAL_MSGQ_NO_WAIT);
    }
    int ret = osal_msg_queue_write_copy(qid, (void *)msg, size, OSAL_MSGQ_NO_WAIT);
    osal_irq_restore(irq_sts);
    return ret;
}
