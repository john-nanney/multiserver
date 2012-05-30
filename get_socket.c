
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "get_socket.h"

int get_udp_socket(const char *server, uint16_t port)
{
	struct sockaddr_in addr;
	int fd = -1;
	u_int yes = 1;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("socket() failed: %s\n", strerror(errno));
		return (-1);
	}

	/* allow multiple sockets to use the same PORT number */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		printf("setsockopt() failed: Reusing ADDR failed: %s\n",
		       strerror(errno));
		return (-1);
	}

	/* set up destination address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);	/* N.B.: differs from sender */
	addr.sin_port = htons(port);

	/* bind to receive address */
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind() failed: %s\n", strerror(errno));
		return (-1);
	}

	return (fd);
}
