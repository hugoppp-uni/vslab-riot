/*
 * Copyright (c) 2017 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     vslab-riot
 * @{
 *
 * @file
 * @brief       Network and utility function wrapper code
 *
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */
#include <stdbool.h>
#include <string.h>

#include "log.h"
#include "fmt.h"
#include "msg.h"
#include "net/ipv6/addr.h"
#include "net/gnrc.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/udp.h"
#include "net/sock/udp.h"

#include "elect.h"

#define LISTEN_MSG_QUEUE_SIZE   (8U)
#define LISTEN_STACKSIZE        (THREAD_STACKSIZE_MAIN)

static char server_stack[LISTEN_STACKSIZE];
static kernel_pid_t server_pid = KERNEL_PID_UNDEF;
/* the link local IP address of this node as string */
static ipv6_addr_t ip_addr;
static char ip_addr_str[IPV6_ADDR_MAX_STR_LEN];
static sock_udp_t _sock;

static kernel_pid_t main_pid;

/* --- internal helper functions --- */

void _get_ip_addr(ipv6_addr_t *addr)
{
    LOG_DEBUG("%s: begin\n", __func__);
    ipv6_addr_t ipv6_addrs[GNRC_NETIF_IPV6_ADDRS_NUMOF];
    gnrc_netif_t *netif = NULL;

    while ((netif = gnrc_netif_iter(netif))) {
        int res = gnrc_netapi_get(netif->pid, NETOPT_IPV6_ADDR, 0, ipv6_addrs,
                                  sizeof(ipv6_addrs));
        if (res > 0) {
            for (unsigned i = 0; i < (res / sizeof(ipv6_addr_t)); ++i) {
                if (ipv6_addr_is_link_local(&ipv6_addrs[i])) {
                    LOG_DEBUG("%s: done\n", __func__);
                    memcpy(addr, &ipv6_addrs[i], sizeof(ipv6_addr_t));
                    return;
                }
            }
        }
    }
    LOG_ERROR("%s: get_node_addr: failed!\n", __func__);
    ipv6_addr_set_unspecified(addr);
}

static void *_listen_loop(void *arg)
{
    (void)arg;

    msg_t msg_queue[LISTEN_MSG_QUEUE_SIZE];

    msg_init_queue(msg_queue, LISTEN_MSG_QUEUE_SIZE);

    while (1) {
        uint8_t buf[IPV6_ADDR_MAX_STR_LEN];
        sock_udp_ep_t remote;

        ssize_t res = sock_udp_recv(&_sock, buf, sizeof(buf),
                                    SOCK_NO_TIMEOUT, &remote);
        buf[res] = '\0';
        LOG_DEBUG("%s: received %u byte(s)!\n", __func__, (unsigned)res);
        msg_t m;
        m.type = ELECT_BROADCAST_EVENT;
        m.content.ptr = &buf[0];
        msg_send_receive(&m, &m, main_pid);
    }
    /* never reached */
    return NULL;
}

int _udp_send(ipv6_addr_t addr, uint16_t port, const uint8_t *data, size_t dlen)
{
    sock_udp_ep_t remote;
    remote.family = AF_INET6;
    if (gnrc_netif_numof() == 1) {
        remote.netif = gnrc_netif_iter(NULL)->pid;
    }
    remote.port = port;
    memcpy(&remote.addr.ipv6[0], &addr.u8[0], 16);

    int res = (int)sock_udp_send(&_sock, data, dlen, &remote);
    if (res < 0) {
        LOG_ERROR("%s: failed (%d)\n", __func__, res);
    }
    else {
        LOG_DEBUG("%s: sent %d of %d byte(s)\n", __func__, res, (int)dlen);
    }
    return res;
}

/* --- public interface functions --- */

int listen_init(kernel_pid_t main)
{
    LOG_DEBUG("%s: begin\n", __func__);
    main_pid = main;
    if (server_pid <= KERNEL_PID_UNDEF) {
        server_pid = thread_create(server_stack, sizeof(server_stack),
                                   (THREAD_PRIORITY_MAIN - 1),
                                   THREAD_CREATE_STACKTEST,
                                   _listen_loop, NULL, "listen");
        if (server_pid <= KERNEL_PID_UNDEF) {
            LOG_ERROR("%s: can not start listen thread!\n", __func__);
            return 1;
        }
    }
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}

int net_init(kernel_pid_t main)
{
    LOG_DEBUG("%s: begin\n", __func__);
    main_pid = main;
    _get_ip_addr(&ip_addr);
    if (ipv6_addr_is_unspecified(&ip_addr) ||
        !ipv6_addr_to_str(ip_addr_str, &ip_addr, sizeof(ip_addr_str))) {
        LOG_ERROR("%s: get IP address!\n", __func__);
        return 1;
    }
    else {
        LOG_DEBUG("%s: got IP address: %s\n", __func__, ip_addr_str);
    }

    kernel_pid_t iface = gnrc_netif_iter(NULL)->pid;

    bool autocca = 0;
    int ret = gnrc_netapi_set(iface, NETOPT_AUTOCCA, 0, &autocca, sizeof(autocca));
    if (ret < 0) {
        LOG_ERROR("%s: failed setting AUTOCCA (%i)\n", __func__, ret);
    }

    int16_t txp = 20;
    ret = gnrc_netapi_get(iface, NETOPT_TX_POWER, 0, &txp, sizeof(txp));
    if (ret < 0) {
        LOG_ERROR("%s: failed setting TXPOWER (%i)\n", __func__, ret);
    }
    else {
        LOG_DEBUG("%s: TX-Power: %" PRIi16 "dBm\n", __func__, txp);
    }

    sock_udp_ep_t local;
    memset(&local, 0, sizeof(sock_udp_ep_t));
    local.family = AF_INET6;
    local.netif  = SOCK_ADDR_ANY_NETIF;
    local.port   = ELECT_BC_NODEID_PORT;

    if (sock_udp_create(&_sock, &local, NULL, 0) < 0) {
        LOG_ERROR("%s: cannot create listen sock!\n", __func__);
        return 1;
    }
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}

void get_node_ip_addr(ipv6_addr_t *addr)
{
    memcpy(addr, &ip_addr, sizeof(ip_addr));
}

int ipv6_addr_cmp(const ipv6_addr_t *ip1, const ipv6_addr_t *ip2)
{
    return memcmp(ip1, ip2, sizeof(ipv6_addr_t));
}

int broadcast_id(const ipv6_addr_t *ip)
{
    LOG_DEBUG("%s: begin.\n", __func__);
    ipv6_addr_t bcast_addr = ELECT_BC_NODEID_ADDR;
    char ip_str[IPV6_ADDR_MAX_STR_LEN];
    if (ipv6_addr_to_str(ip_str, ip, sizeof(ip_str)) == NULL) {
        LOG_ERROR("%s: failed to convert IP address!\n", __func__);
        return 1;
    }
    return _udp_send(bcast_addr, ELECT_BC_NODEID_PORT,
                     (uint8_t *)ip_str, strlen(ip_str));
}

int broadcast_sensor(int16_t val)
{
    LOG_DEBUG("%s: begin (val=%"PRIi16").\n", __func__, val);
    ipv6_addr_t bcast_addr = ELECT_BC_SENSOR_ADDR;
    char val_str[ELECT_BC_SENSOR_LEN];
    size_t len = fmt_s16_dec(val_str, val);
    return _udp_send(bcast_addr, ELECT_BC_SENSOR_PORT,
                     (uint8_t *)val_str, len);
}
