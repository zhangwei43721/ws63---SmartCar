#ifndef HTTP_CTRL_SERVICE_H
#define HTTP_CTRL_SERVICE_H
#include <stdbool.h>
#include <stdint.h>

#define HTTP_CTRL_LISTEN_PORT 8080
#define HTTP_CTRL_STACK_SIZE 4096
#define HTTP_CTRL_TASK_PRIORITY 23

void http_ctrl_service_init(void);

#endif
