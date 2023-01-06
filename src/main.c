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
 * @brief       Leader Election Application
 *
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "log.h"

#include "net/gcoap.h"
#include "kernel_types.h"
#include "random.h"

#include "msg.h"
#include "evtimer_msg.h"
#include "xtimer.h"

#include "elect.h"

const float u = 16;

static msg_t _main_msg_queue[ELECT_NODES_NUM];

/**
 * @name event time configuration
 * @{
 */
static evtimer_msg_t evtimer;
static evtimer_msg_event_t interval_event = {
    .event  = { .offset = ELECT_MSG_INTERVAL },
    .msg    = { .type = ELECT_INTERVAL_EVENT}};
static evtimer_msg_event_t leader_timeout_event = {
    .event  = { .offset = ELECT_LEADER_TIMEOUT },
    .msg    = { .type = ELECT_LEADER_TIMEOUT_EVENT}};
static evtimer_msg_event_t leader_threshold_event = {
    .event  = { .offset = ELECT_LEADER_THRESHOLD },
    .msg    = { .type = ELECT_LEADER_THRESHOLD_EVENT}};
/** @} */

bool amITheBoss = false;
bool running = false;
bool gotNeverMessage = true;
float sensorValue = 0;
ipv6_addr_t nodes [ELECT_NODES_NUM];
ipv6_addr_t latestReceivedIP;//highest ip
ipv6_addr_t ownIP;
uint8_t currentNumNodes = 0;
uint8_t sensorsNumReceived = 0;
uint8_t interruptCnt = 0;
uint8_t successfulGets = 0;

/**
 * @brief   Initialise network, coap, and sensor functions
 *
 * @note    This function should be called first to init the system!
 */
int setup(void)
{
    LOG_DEBUG("%s: begin\n", __func__);
    /* avoid unused variable error */
    (void) interval_event;
    (void) leader_timeout_event;
    (void) leader_threshold_event;

    msg_init_queue(_main_msg_queue, ELECT_NODES_NUM);
    kernel_pid_t main_pid = thread_getpid();

    if (net_init(main_pid) != 0) {
        LOG_ERROR("init network interface!\n");
        return 2;
    }
    if (coap_init(main_pid) != 0) {
        LOG_ERROR("init CoAP!\n");
        return 3;
    }
    if (sensor_init() != 0) {
        LOG_ERROR("init sensor!\n");
        return 4;
    }
    if (listen_init(main_pid) != 0) {
        LOG_ERROR("init listen!\n");
        return 5;
    }
    LOG_DEBUG("%s: done\n", __func__);
    evtimer_init_msg(&evtimer);
    /* send initial `TICK` to start eventloop */
    msg_send(&interval_event.msg, main_pid);
    return 0;
}

void startTimer(uint32_t eventType) {
    kernel_pid_t main_pid = thread_getpid();
    uint32_t eventOffset = 0;
    evtimer_msg_event_t* eventPtr;
        switch(eventType) {
        case ELECT_INTERVAL_EVENT:
            eventPtr = &interval_event;
            eventOffset = ELECT_MSG_INTERVAL;
            break;
        case ELECT_LEADER_THRESHOLD_EVENT:
            eventPtr = &leader_threshold_event;
            eventOffset = ELECT_LEADER_THRESHOLD;
            break;
        case ELECT_LEADER_TIMEOUT_EVENT:
            eventPtr = &leader_timeout_event;
            eventOffset = ELECT_LEADER_TIMEOUT;
            break;
        default:
            printf("### startTimer(): undefined type: <%lu> ###\n", (unsigned long)eventType);
            return; // DO NOT DELETE the return statement.
    }
    evtimer_del(&evtimer, &(eventPtr->event));
    eventPtr->event.offset = eventOffset;
    evtimer_add_msg(&evtimer, eventPtr, main_pid);
}

void reset(void){
    LOG_DEBUG("Resetting...\n");
    currentNumNodes = 0;
    amITheBoss = false;
    running = false;
    gotNeverMessage = true;
    sensorsNumReceived = 0;
    latestReceivedIP = ownIP;
    interruptCnt = 0;
    successfulGets = 0;
    evtimer_del(&evtimer, &interval_event.event);
    evtimer_del(&evtimer, &leader_threshold_event.event);
    evtimer_del(&evtimer, &leader_timeout_event.event);
    startTimer(ELECT_INTERVAL_EVENT);
    startTimer(ELECT_LEADER_THRESHOLD_EVENT);
}

int main(void)
{
    /* this should be first */
    if (setup() != 0) {
        return 1;
    }
    get_node_ip_addr(&ownIP);
    char ip_string[64];
    ipv6_addr_to_str(ip_string, &ownIP, 64);
    LOG_DEBUG("own ip: [%s]\n",ip_string);

    reset();

    while(true) {
        msg_t m;
        msg_receive(&m);

        switch (m.type) {
        case ELECT_INTERVAL_EVENT:
            LOG_DEBUG("+ interval event.\n");
            if(amITheBoss){
                sensorValue = (float)sensor_read();
                for(int i = 0; i<currentNumNodes; i++){
                    if(coap_get_sensor(nodes[i])){
                        reset();
                        break;
                    }
                }
            }else{
                broadcast_id(&ownIP);
            }
            startTimer(ELECT_INTERVAL_EVENT);
            break;

        case ELECT_BROADCAST_EVENT:
            LOG_DEBUG("+ broadcast event, from [%s]", (char *)m.content.ptr);
            if(running){ 
                reset();
                LOG_DEBUG("resetted by broadcast\n");
                break;
            }
            ipv6_addr_t addr;
            char *addr_str = m.content.ptr;
            ipv6_addr_from_str(&addr, addr_str);
            if (ipv6_addr_cmp(&ownIP, &addr) < 0) {
                evtimer_del(&evtimer, &interval_event.event);
                LOG_DEBUG("Received higher ip\n");
            }
            if(gotNeverMessage) {
                latestReceivedIP = addr;
                gotNeverMessage = false;
                startTimer(ELECT_LEADER_THRESHOLD_EVENT);
            }
            if((ipv6_addr_cmp(&latestReceivedIP, &addr) != 0) && (ipv6_addr_cmp(&latestReceivedIP, &addr) < 0)) {
                //other_ip is different from lastRecvIp
                latestReceivedIP = addr;
                startTimer(ELECT_LEADER_THRESHOLD_EVENT);
            }
            break;

        case ELECT_LEADER_ALIVE_EVENT:
            LOG_DEBUG("+ leader event.\n");
            startTimer(ELECT_LEADER_TIMEOUT_EVENT);
            break;
        case ELECT_LEADER_TIMEOUT_EVENT:
            LOG_DEBUG("+ leader timeout event.\n");
            reset();
            break;

        case ELECT_NODES_EVENT:
            LOG_DEBUG("+ nodes event, from [%s].\n", (char *)m.content.ptr);
            if(currentNumNodes >= ELECT_NODES_NUM){
                LOG_ERROR("too many nodes received\n");
                break;
            }
            running = true;
            //check if plausible:
            //compare own ip with highest, if not highest, something weird is happening
            amITheBoss = true;
            ipv6_addr_t ip_addr;
            char *ip_addr_str = m.content.ptr;
            ipv6_addr_from_str(&ip_addr, ip_addr_str);
            nodes[currentNumNodes] = ip_addr;
            currentNumNodes++;
            startTimer(ELECT_INTERVAL_EVENT);
            break;

        case ELECT_SENSOR_EVENT:
            LOG_DEBUG("+ sensor event, value=%s\n",  (char *)m.content.ptr);
            if(amITheBoss){
                sensorsNumReceived++;
                int16_t value = (int16_t)strtol((char *)m.content.ptr, NULL, 10);
                sensorValue = ((u-1)/u)*sensorValue+(1/u)*value;
                if(sensorsNumReceived==currentNumNodes){
                    broadcast_sensor((uint16_t)sensorValue);
                    sensorsNumReceived = 0;
                    sensorValue = 0;
                }
            }
            break;

        case ELECT_LEADER_THRESHOLD_EVENT:
            LOG_DEBUG("+ leader threshold event.\n");
            if(ipv6_addr_cmp(&ownIP,&latestReceivedIP)<0){
                running = true;
                coap_put_node(latestReceivedIP,ownIP);
            }else{
                LOG_WARNING("Leader cannot be alone in channel");
            }
            break;

        default:
            LOG_WARNING("??? invalid event (%x) ???\n", m.type);
            break;
        }
        /* !!! DO NOT REMOVE !!! */
        if ((m.type != ELECT_INTERVAL_EVENT) &&
            (m.type != ELECT_LEADER_TIMEOUT_EVENT) &&
            (m.type != ELECT_LEADER_THRESHOLD_EVENT)) {
            msg_reply(&m, &m);
        }
    }
    /* should never be reached */
    return 0;
}
