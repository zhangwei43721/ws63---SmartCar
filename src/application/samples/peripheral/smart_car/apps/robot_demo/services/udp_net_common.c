#include "udp_net_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/wifi_client/bsp_wifi_sta.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"

uint8_t udp_net_common_checksum8_add(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    if (data && len) {
        for (size_t i = 0; i < len; i++)
            sum += data[i];
    }
    return sum;
}

int udp_net_get_mac_address(uint8_t *mac_buf)
{
    if (!mac_buf)
        return -1;

    struct netif *netif_p = netifapi_netif_find("wlan0");
    if (netif_p == NULL)
        netif_p = netifapi_netif_find("ap0");

    if (netif_p) {
        memcpy_s(mac_buf, 6, netif_p->hwaddr, 6);
        return 0;
    }
    return -1;
}

int udp_net_common_send_broadcast(int fd, const void *buf, size_t len, uint16_t port)
{
    if (fd < 0 || !buf || len == 0)
        return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST);
    return (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr));
}

int udp_net_common_send_to_addr(int fd, const void *buf, size_t len, const struct sockaddr_in *addr)
{
    if (fd < 0 || !buf || len == 0 || !addr)
        return -1;
    return (int)lwip_sendto(fd, buf, len, 0, (struct sockaddr *)addr, sizeof(*addr));
}

void http_send_response_and_close(int client_fd, const char *response)
{
    if (client_fd < 0 || response == NULL)
        return;
    (void)lwip_send(client_fd, response, strlen(response), 0);
    lwip_close(client_fd);
}

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
