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
 * @brief       CoAP wrapper code
 *
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "fmt.h"
#include "msg.h"
#include "net/gcoap.h"
#include "net/ipv6/addr.h"

#include "elect.h"

#define ELECT_COAP_PORT         (5683U)
#define ELECT_COAP_PATH_NODES   ("/nodes")
#define ELECT_COAP_PATH_SENSOR  ("/sensor")

static void _resp_handler(unsigned req_state, coap_pkt_t* pdu, sock_udp_ep_t *remote);
static ssize_t _nodes_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _sensor_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);

/* CoAP resources */
static const coap_resource_t _resources[] = {
    { ELECT_COAP_PATH_NODES,  COAP_PUT,  _nodes_handler, NULL },
    { ELECT_COAP_PATH_SENSOR, COAP_GET,  _sensor_handler, NULL },
};

static gcoap_listener_t _listener = {
    .resources = (coap_resource_t *)&_resources[0],
    .resources_len = sizeof(_resources) / sizeof(_resources[0]),
    .next = NULL
};

static kernel_pid_t main_pid;

static void _resp_handler(unsigned req_state, coap_pkt_t* pdu,
                          sock_udp_ep_t *remote)
{
    LOG_DEBUG("%s: begin\n", __func__);
    (void)remote;
    msg_t sensor_msg = { .type = ELECT_SENSOR_EVENT };

    if (req_state == GCOAP_MEMO_TIMEOUT) {
        LOG_ERROR("gcoap: timeout for msg ID %02u\n", coap_get_id(pdu));
        return;
    }
    else if (req_state == GCOAP_MEMO_ERR) {
        LOG_ERROR("gcoap: error in response\n");
        return;
    }

    char *class_str = (coap_get_code_class(pdu) == COAP_CLASS_SUCCESS)
                            ? "Success" : "Error";
    LOG_DEBUG("gcoap: response %s, code %1u.%02u", class_str,
                                                   coap_get_code_class(pdu),
                                                   coap_get_code_detail(pdu));
    if (pdu->payload_len) {
        unsigned content_type = coap_get_content_type(pdu);
        if (content_type == COAP_FORMAT_TEXT) {
            sensor_msg.content.ptr = pdu->payload;
            msg_send_receive(&sensor_msg, &sensor_msg, main_pid);
        }
        else if ((content_type == COAP_FORMAT_LINK) ||
                 (coap_get_code_class(pdu) == COAP_CLASS_CLIENT_FAILURE) ||
                 (coap_get_code_class(pdu) == COAP_CLASS_SERVER_FAILURE)) {
            /* Expecting diagnostic payload in failure cases */
            LOG_DEBUG("\n%.*s\n", pdu->payload_len, (char *)pdu->payload);
        }
        LOG_DEBUG(" with %u bytes\n", pdu->payload_len);
    }
    else {
        LOG_DEBUG("\n");
    }
    LOG_DEBUG("%s: done\n", __func__);
}

static ssize_t _nodes_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;

    LOG_DEBUG("%s: begin (buflen=%u)\n", __func__, (unsigned)len);
    /* read coap method type in packet */
    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));
    msg_t nodes_msg = { .type = ELECT_NODES_EVENT };
    //ipv6_addr_t addr;
    switch(method_flag) {
        case COAP_PUT:
            LOG_DEBUG("%s: received put with payload: %s\n", __func__, (char *)pdu->payload);
            if (pdu->payload_len > 6) {
                nodes_msg.content.ptr = pdu->payload;
                msg_send_receive(&nodes_msg, &nodes_msg, main_pid);
                return gcoap_response(pdu, buf, len, COAP_CODE_CHANGED);
            }
            else {
                return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
            }
    }
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}

static ssize_t _sensor_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;

    LOG_DEBUG("%s: begin (buflen=%u)\n", __func__, (unsigned)len);
    msg_t leader_msg = { .type = ELECT_LEADER_ALIVE_EVENT };
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    /* write the RIOT board name in the response buffer */
    int16_t val = sensor_read();
    size_t plen = fmt_s16_dec((char *)pdu->payload, val);
    pdu->payload[plen++] = '\0';
    msg_send_receive(&leader_msg, &leader_msg, main_pid);
    LOG_DEBUG("%s: done\n", __func__);
    return gcoap_finish(pdu, plen, COAP_FORMAT_TEXT);;
}

static size_t _send(const uint8_t *buf, size_t len, const ipv6_addr_t *addr)
{
    LOG_DEBUG("%s: begin\n", __func__);
    sock_udp_ep_t remote;

    remote.family   = AF_INET6;
    remote.netif    = SOCK_ADDR_ANY_NETIF;
    if (gnrc_netif_numof() == 1) {
        remote.netif = gnrc_netif_iter(NULL)->pid;
    }
    remote.port     = ELECT_COAP_PORT;

    memcpy(&remote.addr.ipv6[0], &addr->u8[0], sizeof(addr->u8));
    LOG_DEBUG("%s: done\n", __func__);
    return gcoap_req_send2(buf, len, &remote, _resp_handler);
}

/* --- public coap interface --- */

int coap_put_node(ipv6_addr_t addr, ipv6_addr_t node)
{
    LOG_DEBUG("%s: begin\n", __func__);
    uint8_t buf[GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;
    size_t len;

    gcoap_req_init(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE,
                   COAP_METHOD_PUT, ELECT_COAP_PATH_NODES);
    char ipbuf[IPV6_ADDR_MAX_STR_LEN];
    if (ipv6_addr_to_str(ipbuf, &node, sizeof(ipbuf)) == NULL) {
        LOG_ERROR("%s: ipv6_addr_to_str failed!\n", __func__);
        return 1;
    }
    len = strlen(ipbuf);
    memcpy(pdu.payload, ipbuf, len);
    pdu.payload[len++] = '\0';
    len = gcoap_finish(&pdu, len, COAP_FORMAT_TEXT);

    if (!_send(&buf[0], len, &addr)) {
        LOG_ERROR("%s: send failed!\n", __func__);
        return 2;
    }
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}

int coap_get_sensor(ipv6_addr_t addr)
{
    LOG_DEBUG("%s: begin\n", __func__);
    uint8_t buf[GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;
    size_t len = gcoap_request(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE,
                               COAP_METHOD_GET, ELECT_COAP_PATH_SENSOR);

    if (!_send(&buf[0], len, &addr)) {
        LOG_ERROR("%s: send failed!\n", __func__);
        return 1;
    }
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}

int coap_init(kernel_pid_t main)
{
    main_pid = main;
    LOG_DEBUG("%s: begin\n", __func__);
    gcoap_register_listener(&_listener);
    LOG_DEBUG("%s: done\n", __func__);
    return 0;
}
