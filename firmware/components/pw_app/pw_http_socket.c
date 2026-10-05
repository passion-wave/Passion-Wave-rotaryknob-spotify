// SPDX-License-Identifier: MIT
#include "pw_http_socket.h"
#ifndef ESP_PLATFORM
#include <arpa/inet.h>
#include <netinet/tcp.h>
#else
#include "lwip/tcp.h"
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>

bool pw_http_socket_low_latency(int socket_fd) {
    const int enabled = 1;
    return setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof enabled) == 0;
}

bool pw_http_sockaddr_ipv4(const struct sockaddr *address, socklen_t length,
                          struct in_addr *ipv4) {
    if (!ipv4)
        return false;
    memset(ipv4, 0, sizeof *ipv4);
    if (!address || length < offsetof(struct sockaddr, sa_family) + sizeof address->sa_family)
        return false;
    sa_family_t family;
    memcpy(&family, (const unsigned char *)address + offsetof(struct sockaddr, sa_family),
           sizeof family);
    if (family == AF_INET) {
        if (length < sizeof(struct sockaddr_in))
            return false;
        struct sockaddr_in endpoint;
        memcpy(&endpoint, address, sizeof endpoint);
        *ipv4 = endpoint.sin_addr;
        return true;
    }
#if !defined(ESP_PLATFORM) || CONFIG_LWIP_IPV6
    if (family == AF_INET6) {
        if (length < sizeof(struct sockaddr_in6))
            return false;
        struct sockaddr_in6 endpoint;
        memcpy(&endpoint, address, sizeof endpoint);
        /* Accept only RFC 4291 IPv4-mapped addresses, not IPv4-compatible
         * addresses, native IPv6, NAT64 or an arbitrary low 32-bit suffix. */
        static const unsigned char prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (memcmp(endpoint.sin6_addr.s6_addr, prefix, sizeof prefix))
            return false;
        memcpy(ipv4, endpoint.sin6_addr.s6_addr + sizeof prefix, sizeof *ipv4);
        return true;
    }
#endif
    return false;
}

bool pw_http_socket_ipv4(int socket_fd, bool peer, struct in_addr *ipv4) {
    if (!ipv4)
        return false;
    memset(ipv4, 0, sizeof *ipv4);
    struct sockaddr_storage endpoint = {0};
    socklen_t length = sizeof endpoint;
    int result = peer ? getpeername(socket_fd, (struct sockaddr *)&endpoint, &length)
                      : getsockname(socket_fd, (struct sockaddr *)&endpoint, &length);
    return result == 0 && length <= sizeof endpoint &&
           pw_http_sockaddr_ipv4((const struct sockaddr *)&endpoint, length, ipv4);
}

bool pw_http_host_matches_ipv4(const char *host, const struct in_addr *ipv4) {
    if (!host || !ipv4 || ipv4->s_addr == htonl(INADDR_ANY))
        return false;
    char ip[INET_ADDRSTRLEN], with_port[INET_ADDRSTRLEN + 3];
    if (!inet_ntop(AF_INET, ipv4, ip, sizeof ip))
        return false;
    snprintf(with_port, sizeof with_port, "%s:80", ip);
    return !strcmp(host, ip) || !strcmp(host, with_port);
}
