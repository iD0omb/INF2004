#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

// allow override in some examples
#ifndef NO_SYS
#define NO_SYS                          1
#endif

// allow override in some examples
#ifndef LWIP_SOCKET
#define LWIP_SOCKET                     0
#endif

#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC                 1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC                 0
#endif

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        24000          // Increased from 4000
#define MEMP_NUM_TCP_SEG                64            // Increased to 64 (MUST be >= TCP_SND_QUEUELEN)
#define MEMP_NUM_ARP_QUEUE              10
#define PBUF_POOL_SIZE                  64            // Increased from 24
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define TCP_WND                         (16 * TCP_MSS) // Increased from 8
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (16 * TCP_MSS) // Increased from 8
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETCONN                    0
#define MEM_STATS                       0
#define SYS_STATS                       0
#define MEMP_STATS                      0
#define LINK_STATS                      0
// #define ETH_PAD_SIZE                 2
#define LWIP_CHKSUM_ALGORITHM           3
#define LWIP_DHCP                       1
#define LWIP_IPV4                       1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DNS                        1
#define LWIP_TCP_KEEPALIVE              1
#define LWIP_NETIF_TX_SINGLE_PBUF       1
#define DHCP_DOES_ARP_CHECK             0
#define LWIP_DHCP_DOES_ACD_CHECK        0

#ifndef NDEBUG
#define LWIP_DEBUG                      1
#define LWIP_STATS                      1
#define LWIP_STATS_DISPLAY              1
#endif

#define ETHARP_DEBUG                    LWIP_DBG_OFF
#define NETIF_DEBUG                     LWIP_DBG_OFF
#define PBUF_DEBUG                      LWIP_DBG_OFF
#define API_LIB_DEBUG                   LWIP_DBG_OFF
#define API_MSG_DEBUG                   LWIP_DBG_OFF
#define SOCKETS_DEBUG                   LWIP_DBG_OFF
#define ICMP_DEBUG                      LWIP_DBG_OFF
#define INET_DEBUG                      LWIP_DBG_OFF
#define IP_DEBUG                        LWIP_DBG_OFF
#define IP_REASS_DEBUG                  LWIP_DBG_OFF
#define RAW_DEBUG                       LWIP_DBG_OFF
#define MEM_DEBUG                       LWIP_DBG_OFF
#define MEMP_DEBUG                      LWIP_DBG_OFF
#define SYS_DEBUG                       LWIP_DBG_OFF
#define TCP_DEBUG                       LWIP_DBG_OFF
#define TCP_INPUT_DEBUG                 LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG                LWIP_DBG_OFF
#define TCP_RTO_DEBUG                   LWIP_DBG_OFF
#define TCP_CWND_DEBUG                  LWIP_DBG_OFF
#define TCP_WND_DEBUG                   LWIP_DBG_OFF
#define TCP_FR_DEBUG                    LWIP_DBG_OFF
#define TCP_QLEN_DEBUG                  LWIP_DBG_OFF
#define TCP_RST_DEBUG                   LWIP_DBG_OFF
#define UDP_DEBUG                       LWIP_DBG_OFF
#define TCPIP_DEBUG                     LWIP_DBG_OFF
#define PPP_DEBUG                       LWIP_DBG_OFF
#define SLIP_DEBUG                      LWIP_DBG_OFF
#define DHCP_DEBUG                      LWIP_DBG_OFF

// MQTT and HTTP Server settings
#define LWIP_MQTT                       1              // Enable MQTT
#define MEMP_NUM_SYS_TIMEOUT            16             // Increased from 12
#define MEMP_NUM_NETBUF                 16             // Added for better buffer management
#define MEMP_NUM_NETCONN                8              // Added for connection management
#define TCP_LISTEN_BACKLOG              1              // Enable listen backlog
#define LWIP_SO_RCVTIMEO                1              // Enable receive timeout
#define LWIP_SO_SNDTIMEO                1              // Enable send timeout
#define MQTT_OUTPUT_RINGBUF_SIZE        2048            // MQTT output buffer
#define MQTT_REQ_MAX_IN_FLIGHT          4              // Max in-flight requests


// Memory pools for concurrent connections
#define MEMP_NUM_TCP_PCB                8              // Max TCP connections
#define MEMP_NUM_TCP_PCB_LISTEN         2              // Listening connections

#endif /* __LWIPOPTS_H__ */