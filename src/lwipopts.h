/**
 * lwIP Configuration for MIA
 * Optimized for video streaming over Wi-Fi
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Platform specific locking
#define NO_SYS                     1
#define LWIP_SOCKET                0
#define LWIP_NETCONN               0

// Memory options
#define MEM_LIBC_MALLOC            0
#define MEMP_MEM_MALLOC            0
#define MEM_ALIGNMENT              4
#define MEM_SIZE                   4000
#define MEMP_NUM_PBUF              10
#define MEMP_NUM_UDP_PCB           6
#define MEMP_NUM_TCP_PCB           10
#define MEMP_NUM_TCP_PCB_LISTEN    8
#define MEMP_NUM_TCP_SEG           16
#define MEMP_NUM_REASSDATA         3
#define MEMP_NUM_FRAG_PBUF         15
#define MEMP_NUM_ARP_QUEUE         10
#define MEMP_NUM_IGMP_GROUP        8

// Pbuf options
#define PBUF_POOL_SIZE             10
#define PBUF_POOL_BUFSIZE          592

// ARP options
#define LWIP_ARP                   1
#define ARP_TABLE_SIZE             10
#define ARP_QUEUEING               0

// IP options
#define IP_FORWARD                 0
#define IP_OPTIONS_ALLOWED         1
#define IP_REASSEMBLY              1
#define IP_FRAG                    1
#define IP_REASS_MAXAGE            3
#define IP_REASS_MAX_PBUFS         8
#define IP_FRAG_USES_STATIC_BUF    0
#define IP_DEFAULT_TTL             255
#define IP_SOF_BROADCAST           0
#define IP_SOF_BROADCAST_RECV      0

// ICMP options
#define LWIP_ICMP                  1
#define ICMP_TTL                   255
#define LWIP_BROADCAST_PING        0
#define LWIP_MULTICAST_PING        0

// DHCP options
#define LWIP_DHCP                  1
#define DHCP_DOES_ARP_CHECK        0

// UDP options
#define LWIP_UDP                   1
#define LWIP_UDPLITE               0
#define UDP_TTL                    255
#define LWIP_NETBUF_RECVINFO       0

// TCP options
#define LWIP_TCP                   1
#define TCP_TTL                    255
#define TCP_WND                    2048
#define TCP_MAXRTX                 12
#define TCP_SYNMAXRTX              6
#define TCP_QUEUE_OOSEQ            1
#define TCP_MSS                    536
#define TCP_CALCULATE_EFF_SEND_MSS 1
#define TCP_SND_BUF                2048
#define TCP_SND_QUEUELEN           (4 * (TCP_SND_BUF) / (TCP_MSS))
#define TCP_SNDLOWAT               ((TCP_SND_BUF)/2)
#define TCP_LISTEN_BACKLOG         0
#define LWIP_TCP_TIMESTAMPS        0
#define TCP_WND_UPDATE_THRESHOLD   (TCP_WND / 4)

// Network interface options
#define LWIP_NETIF_HOSTNAME        1
#define LWIP_NETIF_API             0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_NETIF_REMOVE_CALLBACK 0
#define LWIP_NETIF_HWADDRHINT      0
#define LWIP_NETIF_LOOPBACK        0
#define LWIP_LOOPBACK_MAX_PBUFS    0

// Thread options
#define TCPIP_THREAD_NAME          "tcpip_thread"
#define TCPIP_THREAD_STACKSIZE     1024
#define TCPIP_THREAD_PRIO          1
#define TCPIP_MBOX_SIZE            8
#define DEFAULT_UDP_RECVMBOX_SIZE  6
#define DEFAULT_TCP_RECVMBOX_SIZE  6
#define DEFAULT_ACCEPTMBOX_SIZE    6
#define DEFAULT_THREAD_STACKSIZE   512
#define DEFAULT_THREAD_PRIO        1

// Sequential layer options
#define LWIP_TCPIP_CORE_LOCKING    0

// Socket options
#define LWIP_COMPAT_SOCKETS        0

// Statistics options
#define LWIP_STATS                 0

// PPP options
#define PPP_SUPPORT                0

// Checksum options
#define CHECKSUM_GEN_IP            1
#define CHECKSUM_GEN_UDP           1
#define CHECKSUM_GEN_TCP           1
#define CHECKSUM_CHECK_IP          1
#define CHECKSUM_CHECK_UDP         1
#define CHECKSUM_CHECK_TCP         1

// IPv6 options
#define LWIP_IPV6                  0

// Hook options (disabled for now)
// #define LWIP_HOOK_FILENAME         "lwiphooks.h"

// Debugging options
#define LWIP_DEBUG                 0

// Performance tracking
#define LWIP_PERF                  0

#endif // LWIPOPTS_H