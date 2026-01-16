#ifndef HTTP_CTRL_SERVICE_H
#define HTTP_CTRL_SERVICE_H
#include "../core/robot_mgr.h"
#include "net_service.h"

#include "securec.h"
#include "soc_osal.h"

#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_CTRL_LISTEN_PORT 8080
#define HTTP_CTRL_STACK_SIZE 4096
#define HTTP_CTRL_TASK_PRIORITY 23

void http_ctrl_service_init(void);

#endif
