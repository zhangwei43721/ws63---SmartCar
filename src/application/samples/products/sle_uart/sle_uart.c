#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "pinctrl.h"
#include "uart.h"
// #include "pm_clock.h"
#include "sle_low_latency.h"

#include "sle_low_latency.h"
#if defined(CONFIG_SAMPLE_SUPPORT_SLE_UART_SERVER)
#include "securec.h"
#include "sle_uart_server.h"
#include "sle_uart_server_adv.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_UART_CLIENT)
#define SLE_UART_TASK_STACK_SIZE            0x600
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_uart_client.h"
#endif  /* CONFIG_SAMPLE_SUPPORT_SLE_UART_CLIENT */

#define SLE_UART_TASK_PRIO                  28
#define SLE_UART_TASK_DURATION_MS           2000
#define SLE_UART_BAUDRATE                   115200
#define SLE_UART_TRANSFER_SIZE              512



#define SLE_UART_SERVER_DELAY_COUNT         5

#define SLE_UART_TASK_STACK_SIZE            0x1200
#define SLE_ADV_HANDLE_DEFAULT              1
#define SLE_UART_SERVER_MSG_QUEUE_LEN       5
#define SLE_UART_SERVER_MSG_QUEUE_MAX_SIZE  32 // 消息队列节点大小
#define SLE_UART_SERVER_QUEUE_DELAY         0xFFFFFFFF
#define SLE_UART_SERVER_BUFF_MAX_SIZE       800

unsigned long g_sle_uart_server_msgqueue_id; // 消息队列ID

#define SLE_UART_SERVER_LOG  "[sle uart server]"

static void sle_uart_server_create_msgqueue(void)
{
    if (osal_msg_queue_create("sle_uart_server_msgqueue", SLE_UART_SERVER_MSG_QUEUE_LEN, \
                                    (unsigned long *)&g_sle_uart_server_msgqueue_id, 0,  SLE_UART_SERVER_MSG_QUEUE_MAX_SIZE) != OSAL_SUCCESS) {
        osal_printk("^%s sle_uart_server_create_msgqueue message queue create failed!\n", SLE_UART_SERVER_LOG);
    }
}


//#if defined(CONFIG_UART_SUPPORT_TX)
int32_t uapi_uart_write(uart_bus_t bus, const uint8_t *buffer, uint32_t length, uint32_t timeout)
{
    unused(timeout);
    bool tx_fifo_full = false;
    uint8_t *data_buffer = (uint8_t *)buffer;
    int32_t write_count = 0;
    uint32_t len = length;

    int32_t ret = uapi_uart_param_check(bus, buffer, length);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    uint32_t irq_sts = uart_porting_lock(bus);
    while (len > 0) {
        hal_uart_ctrl(bus, UART_CTRL_CHECK_TX_FIFO_FULL, (uintptr_t)&tx_fifo_full);
        if (tx_fifo_full == false) {
            hal_uart_write(bus, data_buffer++, 1);
            len--;
            write_count++;
        }
    }
    uart_porting_unlock(bus, irq_sts);

    return write_count;
}
//#endif

// 回调函数--服务器用于写消息队列
static void sle_uart_server_write_msgqueue(uint8_t *buffer_addr, uint16_t buffer_size)
{
    // 发送消息队列尾部
    osal_msg_queue_write_copy(g_sle_uart_server_msgqueue_id, (void *)buffer_addr, (uint32_t)buffer_size, 0);
}

// 回调函数-读星闪信息
static void ssaps_server_read_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para,
    errcode_t status)
{
    osal_printk("%s ssaps read request cbk callback server_id:%x, conn_id:%x, handle:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, conn_id, read_cb_para->handle, status);
}

// 回调函数-- 服务写入请求
static void ssaps_server_write_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para,
    errcode_t status)
{
    osal_printk("%s ssaps write request callback cbk server_id:%x, conn_id:%x, handle:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, conn_id, write_cb_para->handle, status);
    if ((write_cb_para->length > 0) && write_cb_para->value) {
        osal_printk("\n sle uart recived data : %s\r\n", write_cb_para->value);
        uapi_uart_write(CONFIG_SLE_UART_BUS, (uint8_t *)write_cb_para->value, write_cb_para->length, 0);
    }
}

static void *sle_uart_server_task(const char *arg)
{
    unused(arg);
    uint8_t rx_buf[SLE_UART_SERVER_BUFF_MAX_SIZE] = {0};
    uint32_t rx_length = SLE_UART_SERVER_BUFF_MAX_SIZE;
    uint8_t sle_connect_state[] = "sle_dis_connect";

    // 创建星闪串口服务消息队列
    sle_uart_server_create_msgqueue();

    // 登记sle_uart_server_write_msgqueue，用于回调
    sle_uart_server_register_msg(sle_uart_server_write_msgqueue);
    // 初始化星闪服务
    sle_uart_server_init(ssaps_server_read_request_cbk, ssaps_server_write_request_cbk);


}


static void sle_uart_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    // 选择客户端还是服务器，通过配置菜单进行选择
#if defined(CONFIG_SAMPLE_SUPPORT_SLE_UART_SERVER)
    task_handle = osal_kthread_create((osal_kthread_handler)sle_uart_server_task, 0, "SLEUartServerTask", SLE_UART_TASK_STACK_SIZE);

#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_UART_CLIENT)
    task_handle = osal_kthread_create((osal_kthread_handler)sle_uart_client_task, 0, "SLEUartDongleTask",
                                      SLE_UART_TASK_STACK_SIZE);
#endif /* CONFIG_SAMPLE_SUPPORT_SLE_UART_CLIENT */
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, SLE_UART_TASK_PRIO);
    }
    osal_kthread_unlock();

}

// 程序入口
app_run(sle_uart_entry);


