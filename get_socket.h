
#ifdef __GNUC__
#pragma once
#endif				/* __GNUC__ */

#ifndef GET_SOCKET_H
#define GET_SOCKET_H 1

#include <stdint.h>

int get_udp_socket(const char *server, uint16_t port);

#endif				/* GET_SOCKET_H */
