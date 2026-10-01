// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/* IPv4 and IPv4-mapped IPv6 are the same IPv4 endpoint on a dual-stack
 * listener. Native IPv6 is not an IPv4 setup peer. Output is network order. */
bool pw_http_sockaddr_ipv4(const struct sockaddr *address, socklen_t length,
                          struct in_addr *ipv4);
bool pw_http_socket_ipv4(int socket_fd, bool peer, struct in_addr *ipv4);
bool pw_http_host_matches_ipv4(const char *host, const struct in_addr *ipv4);
