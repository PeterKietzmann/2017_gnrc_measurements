/*
 * Copyright (C) 2015 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief
 *
 * @author      Peter Kietzmann <peter.kietzmann@haw-hamburg.de>
 *
 * @}
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net/gnrc/ipv6.h"
#include "net/ipv6/addr.h"

#include "kernel_types.h"

#include "thread.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"

#ifndef NUM_PACKETS
#define NUM_PACKETS         (3)
#endif
#ifndef MIN_PACKET_SIZE
#define MIN_PACKET_SIZE     (10)
#endif
#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE     (1211)
#endif
#ifndef STEP_SIZE
#define STEP_SIZE           (10)
#endif
#ifndef DELAY_PACKET_US
#define DELAY_PACKET_US     (0)
#endif
#ifndef DELAY_SIZE_US
#define DELAY_SIZE_US       (0)
#endif
#ifndef MEASURE_MEAN
#define MEASURE_MEAN        (2)
#endif

#define DONT_PRINT_DATA     (0)

#define SERVER_MSG_QUEUE_SIZE               (8)

#define SERVER_BUFFER_SIZE                  (MAX_PACKET_SIZE)

#define SC_NETIF_IPV6_DEFAULT_PREFIX_LEN    (64)

/* Server */
static int server_socket = -1;
static char server_buffer[SERVER_BUFFER_SIZE];
static char server_stack[THREAD_STACKSIZE_DEFAULT];
static msg_t server_msg_queue[SERVER_MSG_QUEUE_SIZE];

/* Measurement stuff MEAN_MODE = 0: Measure each packet and save value
 *                             = 1: Measure all packets and save value
 *                             = 2: Measure each packet but increment all
 */
#if MEASURE_MEAN == 0
static uint32_t buffer_measurement[NUM_PACKETS];

#elif MEASURE_MEAN == 1
#define BUFFER_SIZE ((MAX_PACKET_SIZE - MIN_PACKET_SIZE) / STEP_SIZE)+1
static uint32_t buffer_measurement[BUFFER_SIZE];
static int packet_counter = 0;

#elif MEASURE_MEAN == 2
#define BUFFER_SIZE ((MAX_PACKET_SIZE - MIN_PACKET_SIZE) / STEP_SIZE)+1
static uint32_t buffer_measurement[BUFFER_SIZE];
static int packet_counter = 0;
static uint32_t mean_increment = 0;
static int measure_num = 0;
#endif

static uint32_t start_time = 0;
static int static_idx = -1;

static void *_server_thread(void *args)
{
    (void)args;

    struct sockaddr_in6 server_addr;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);
    server_socket = socket(AF_INET6, SOCK_RAW, GNRC_NETTYPE_UNDEF);

    server_addr.sin6_family = AF_INET6;
    memset(&server_addr.sin6_addr, 0, sizeof(server_addr.sin6_addr));

    if (server_socket < 0) {
        DEBUG("error initializing socket");
        server_socket = 0;
        return NULL;
    }
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        server_socket = -1;
        DEBUG("error binding socket");
        return NULL;
    }
    DEBUG("Success: started IP server\n");
    while (1) {
        int res;
        struct sockaddr_in6 src;
        socklen_t src_len = sizeof(struct sockaddr_in6);
        if ((res = recvfrom(server_socket, server_buffer, sizeof(server_buffer), 0,
                            (struct sockaddr *)&src, &src_len)) < 0) {
            DEBUG("Error on receive\n");
        }
        else if (res == 0) {
            DEBUG("Peer did shut down\n");
        }
        else {
#if MEASURE_MEAN == 0 
                buffer_measurement[static_idx] = xtimer_now() - start_time;
                start_time = 0;

#elif MEASURE_MEAN == 1
                packet_counter++;
                if (packet_counter == NUM_PACKETS) {
                    buffer_measurement[static_idx] = xtimer_now() - start_time;
                    start_time = 0;
                    packet_counter = 0;
                    DEBUG("Wrote %ist buffer val\n", static_idx);
                }

#elif MEASURE_MEAN == 2
                packet_counter++;
                /* compute mean value during runtime*/
                mean_increment = mean_increment + (xtimer_now() - start_time);
                start_time = 0;
                if (packet_counter == NUM_PACKETS) {
                    buffer_measurement[measure_num++] = mean_increment;
                    packet_counter = 0;
                    mean_increment = 0;
                }
#endif
            DEBUG("Received data of size: %i\n", res);
            //puts(server_buffer);
        }
    }
    return NULL;
}

static int udp_start_server(void)
{

    /* check if server is already running */
    if (server_socket >= 0) {
        DEBUG("Error: server already running");
        return 1;
    }
    /* start server (which means registering pktdump for the chosen port) */
    if (thread_create(server_stack, sizeof(server_stack), THREAD_PRIORITY_MAIN - 1,
                      THREAD_CREATE_STACKTEST, _server_thread, NULL, "IP server") <= KERNEL_PID_UNDEF) {
        server_socket = -1;
        DEBUG("error initializing thread");
        return 1;
    }
    return 0;
}

int main(void)
{
    DEBUG("%i iterations for packets with payloads %i:%i:%i\n ", NUM_PACKETS, MIN_PACKET_SIZE, STEP_SIZE, MAX_PACKET_SIZE);
    DEBUG("Delay between to sizes: %i us; Delay between two packets: %i us\n", DELAY_SIZE_US, DELAY_PACKET_US);
    DEBUG("posix_ip; MEASURE_MEAN: %i , LOOPBACK_MODE: %i\n",MEASURE_MEAN, LOOPBACK_MODE);

    udp_start_server();

    char data[MAX_PACKET_SIZE];
    for (int i = 0; i < MAX_PACKET_SIZE; i++) {
        data[i] = i;
    }


    struct sockaddr_in6 dst;// src;
    int s;
    //src.sin6_family = AF_INET6;
    dst.sin6_family = AF_INET6;

    //memset(&src.sin6_addr, 0, sizeof(src.sin6_addr));

#if !LOOPBACK_MODE

    ipv6_addr_t addr;
    ipv6_addr_t dest_addr;
    gnrc_ipv6_nc_t *nc_entry = NULL;
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];

    /* set global unicast SOURCE  address */
    //char addr_str[] = "fe80::3432:4833:46d9:8a13";
    char addr_str[]= "2001:cafe:0000:0002:0222:64af:126b:8a14";

    gnrc_netif_get(ifs);

    /* reset address on interface */
    gnrc_ipv6_netif_reset_addr(ifs[0]);

    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        DEBUG("error: unable to parse IPv6 address.");
        return 1;
    }
    if (gnrc_ipv6_netif_add_addr(ifs[0], &addr, SC_NETIF_IPV6_DEFAULT_PREFIX_LEN, false) == NULL) {
        DEBUG("Error: unable to add IPv6 address");
    }


    /* set global unicast DESTINATION  address */
    char dst_addr_str[] = "2001:cafe:0000:0002:0222:64af:126b:8a14";

    /* set hhardware address of receiver */
    uint8_t hwaddr[8] = {0x10, 0x22, 0x64, 0xaf, 0x12, 0x6b, 0x8a, 0x14};

    if (ipv6_addr_from_str(&dest_addr, dst_addr_str) == NULL) {
        DEBUG("error: unable to parse IPv6 address.");
        return 1;
    }

    if ((nc_entry = gnrc_ipv6_nc_add(ifs[0], &dest_addr, hwaddr, sizeof(hwaddr), 0)) == NULL) {
        DEBUG("error: unable to add address to neighbor cache.");
        return 1;
    }
    else{
        nc_entry->flags &= ~(GNRC_IPV6_NC_STATE_MASK);
        nc_entry->flags |= (GNRC_IPV6_NC_STATE_REACHABLE);
        nc_entry->flags &= ~(GNRC_IPV6_NC_TYPE_MASK);
        nc_entry->flags |= (GNRC_IPV6_NC_TYPE_REGISTERED); /* this is the important thing */
        DEBUG("added address to NC\n");
    }

    /* parse destination address */
    if (inet_pton(AF_INET6, dst_addr_str, &dst.sin6_addr) != 1) {
        DEBUG("Error: unable to parse destination address");
        return 1;
    }

#else
    char dst_addr_str[] = "::1";
    //ipv6_addr_t dest_addr;
/*
    if (ipv6_addr_from_str(&dest_addr, dst_addr_str) == NULL) {
        DEBUG("error: unable to parse IPv6 address.");
        return 1;
    }*/
    /* parse destination address */
    if (inet_pton(AF_INET6, dst_addr_str, &dst.sin6_addr) != 1) {
        DEBUG("Error: unable to parse destination address");
        return 1;
    }

#endif

    /* parse port */
    s = socket(AF_INET6, SOCK_RAW, GNRC_NETTYPE_UNDEF);
    if (s < 0) {
        DEBUG("error initializing socket");
        return 1;
    }

    puts("START");

    for(unsigned int j = MIN_PACKET_SIZE; j < MAX_PACKET_SIZE; j+=STEP_SIZE) {
#if MEASURE_MEAN == 1
        DEBUG("Packet size: %i and static_idx= %i\n", j, static_idx);
        static_idx++;
        start_time = xtimer_now();
#endif
        for (unsigned int i = 0; i < NUM_PACKETS; i++) {
#if MEASURE_MEAN == 0 || MEASURE_MEAN == 2
            static_idx++;
            start_time = xtimer_now();
#endif
            DEBUG("Before sendto %" PRIu32"\n", start_time);
            sendto(s, &data, j, 0, (struct sockaddr *)&dst, sizeof(dst));
            DEBUG("After sendto\n");
#if DELAY_PACKET_US
        xtimer_usleep(DELAY_PACKET_US);
#endif
        }
#if !DONT_PRINT_DATA && !MEASURE_MEAN 
        /* Print measurement array to standard out */
        for(unsigned int i = 0; i < NUM_PACKETS; i++){
            static_idx = -1;
            printf(" %" PRIu32, buffer_measurement[i]);
        }
        puts("");
#endif
#if DELAY_SIZE_US
        xtimer_usleep(DELAY_SIZE_US);
#endif
    }
#if !DONT_PRINT_DATA && MEASURE_MEAN
        /* Print measurement array to standard out */
        for(unsigned int i = 0; i < BUFFER_SIZE; i++){
            printf(" %" PRIu32, buffer_measurement[i]);
        }
        puts("");
#endif /* !DONT_PRINT_DATA && MEASURE_MEAN */
    close(s);
    puts("DONE");

    return 0;
}