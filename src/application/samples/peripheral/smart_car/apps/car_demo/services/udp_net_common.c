#include "udp_net_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lwip/inet.h"
#include "securec.h"

// 通过UDP发送数据到指定地址
int udp_net_common_send_to_addr(int fd, const void *buf, size_t len, const struct sockaddr_in *addr)
{
    if (fd < 0 || !buf || len == 0 || !addr)
        return -1;
    return (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr *)addr, sizeof(*addr));
}

// 发送HTTP响应并关闭连接
void http_send_response_and_close(int client_fd, const char *response)
{
    if (client_fd < 0 || response == NULL)
        return;
    (void)lwip_send(client_fd, response, strlen(response), 0);
    lwip_close(client_fd);
}

#include "portal_html.h"

// 发送 HTML 响应主体并自动加 HTTP 200 OK 头且关闭
void http_send_html_response(int client_fd, const char *html)
{
    if (client_fd < 0 || html == NULL)
        return;
    (void)lwip_send(client_fd, HTTP_OK_HEADER, strlen(HTTP_OK_HEADER), 0);
    (void)lwip_send(client_fd, html, strlen(html), 0);
    lwip_close(client_fd);
}

// 创建UDP socket并绑定端口，设置接收超时和广播选项
int udp_net_common_open_and_bind(uint16_t port, unsigned int recv_timeout_ms, bool enable_broadcast)
{
    int sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return -1;

    struct timeval tv = {0, (int)recv_timeout_ms * 1000};
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (enable_broadcast) {
        int broadcast = 1;
        lwip_setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = IPADDR_ANY;

    if (lwip_bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("udp_net: Bind failed %d\r\n", errno);
        lwip_close(sockfd);
        return -1;
    }

    return sockfd;
}
