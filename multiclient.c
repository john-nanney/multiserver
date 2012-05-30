
#define _LARGEFILE64_SOURCE

#ifndef __USE_GNU
#define __USE_GNU
/* For mkostemp() from stdlib.h */
#endif				/* __USE_GNU */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/epoll.h>
#include <endian.h>

#include "get_socket.h"
#include "mxb_protocol.h"

static char *blockmap = NULL;
static char *progname = NULL;
static char *outputname = NULL;
static char msgbuf[MAX_PACKET_SIZE];
static mxbp_header_t mapreq;
static char *server = NULL;
static uint16_t server_port = DEFAULT_SERVER_PORT;
uint16_t from_port = 0;
static mxbp_map_t mapdesc;
static char recvfilename[FILENAMESIZE];

void usage(void)
{
	printf
	    ("\nUsage: %s [ -p PORT ] [ -g GROUP ] [ -t TIMEOUT ] [ -f FROM_PORT ] [ -s SERVER_PORT ] [ -o OUTPUTFILENAME ]\n\n",
	     progname ? progname : "multiserver");
	printf("PORT is the multicast port to listen on, defaults to %d\n",
	       DEFAULT_PORT);
	printf("GROUP is the multicast address, defaults to \"%s\"\n",
	       DEFAULT_GROUP);
	printf("TIMEOUT is the quiet time in milliseconds, defaults to %d\n",
	       DEFAULT_TIMEOUT);
	printf
	    ("FROM_PORT is the originating port number, not usually needed unless debugging.\n");
	printf
	    ("SERVER_PORT is the port for the block map server, defaults to %d\n",
	     DEFAULT_SERVER_PORT);
	printf
	    ("OUTPUTFILENAME allows the file name to override what the block server sends.\n");
	puts("\n");
	exit(EXIT_FAILURE);
}

int get_multicast_socket(const char *group, unsigned short port)
{
	int fd;
	struct ip_mreq mreq;

	if ((fd = get_udp_socket(group, port)) < 0) {
		printf("Could not get UDP socket.\n");
		return (-1);
	}

	/* use setsockopt() to request that the kernel join a multicast group */
	mreq.imr_multiaddr.s_addr = inet_addr(group);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) <
	    0) {
		printf
		    ("%s:setsockopt() failed: Join multicast group failed: %s\n",
		     progname, strerror(errno));
		return (-1);
	}

	return (fd);
}

void send_mapreq(int s)
{
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(server);
	addr.sin_port = htons(server_port);

	printf("Sending map request to %s port %d (from %d)\n", server,
	       server_port, from_port);
	if (sendto
	    (s, &mapreq, sizeof(mapreq), 0, (struct sockaddr *)&addr,
	     sizeof(addr)) < 0) {
		printf("sendto(): failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void send_block_req(int s)
{
	struct sockaddr_in addr;
	mxbp_packet_t *blockreq;
	char brbuf[20];
	uint32_t *up;
	int x;

	memset(brbuf, 0, sizeof(mxbp_header_t) + (sizeof(uint32_t) * 2));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(server);
	addr.sin_port = htons(server_port);

	blockreq = (mxbp_packet_t *) brbuf;
	blockreq->header.magic = htobe32(MXBP_MAGIC);
	blockreq->header.op = htobe16(MXBP_BLOCKREQ);
	blockreq->header.size = htobe16(sizeof(uint32_t) * 2);
	up = (uint32_t *) blockreq->data;

	{
		char template[1024];
		strcpy(template, "blocklist_XXXXXX.bin");
		int fd = mkostemp(template, O_CREAT);
		write(fd, blockmap, mapdesc.nblocks);
		close(fd);
	}

	/* Find first empty block. */
	for (x = 0; x < mapdesc.nblocks; ++x) {
		if (!blockmap[x]) {
			up[0] = htobe32(x);
			up[1] = htobe32(mapdesc.nblocks);
			break;
		}
	}

	printf("Sending block request from %d to %d\n", be32toh(up[0]),
	       be32toh(up[1]));

	if (sendto
	    (s, blockreq, sizeof(mxbp_header_t) + (sizeof(uint32_t) * 2), 0,
	     (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("sendto(): failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void read_mapdesc(int s)
{
	mxbp_header_t header;
	struct msghdr mh;
	struct sockaddr_in addr;
	struct iovec v[3];
	int bytes;

	memset(&addr, 0, sizeof(addr));
	memset(&mh, 0, sizeof(mh));
	memset(recvfilename, 0, FILENAMESIZE);

	v[0].iov_base = &header;
	v[0].iov_len = sizeof(header);
	v[1].iov_base = &mapdesc;
	v[1].iov_len = sizeof(mapdesc);
	v[2].iov_base = recvfilename;
	v[2].iov_len = FILENAMESIZE - 1;

	mh.msg_name = &addr;
	mh.msg_namelen = sizeof(addr);
	mh.msg_iov = v;
	mh.msg_iovlen = 3;

	bytes = recvmsg(s, &mh, 0);
	if (bytes < 0 || bytes < (sizeof(header) + sizeof(mapdesc))) {
		printf("Bad receive: %d %s\n", bytes, strerror(errno));
		printf("header.magic = 0x%08x\n", be32toh(header.magic));
		printf("header.op = %u\n", be16toh(header.op));
		printf("header.size = %u\n", be16toh(header.size));
		printf("header.blockid = %u\n", be32toh(header.blockid));
		printf("sizeof(header) == %ld\n", sizeof(header));
		memset(&mapdesc, 0, sizeof(mapdesc));
		return;
	}

	header.magic = be32toh(header.magic);
	if (header.magic != MXBP_MAGIC) {
		memset(&mapdesc, 0, sizeof(mapdesc));
		return;
	}
	header.op = be16toh(header.op);
	if (header.op != MXBP_BLOCKDESC) {
		memset(&mapdesc, 0, sizeof(mapdesc));
		return;
	}
	header.size = be16toh(header.size);
	if (header.size > FILENAMESIZE - 1) {
		printf
		    ("Holy crap, we got a horrendous filename size of %d bytes, this can't be right.\n",
		     header.size);
		memset(&mapdesc, 0, sizeof(mapdesc));
		return;
	}

	mapdesc.filesize = be64toh(mapdesc.filesize);
	mapdesc.blocksize = be32toh(mapdesc.blocksize);
	mapdesc.nblocks = be32toh(mapdesc.nblocks);
}

static inline int check_finished(void)
{
	int x;
	for (x = 0; x < mapdesc.nblocks; ++x) {
		if (!blockmap[x]) {
			return (0);
		}
	}
	return (1);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	socklen_t addrlen;
	mxbp_packet_t *block_packet;
	char *group = NULL;
	char *cp;
	uint16_t port = DEFAULT_PORT;
	int nbytes;
	int epollfd;
	int sock;
	int ctlsock;
	int c;
	int rv;
	int fd;
	int timeoutms = DEFAULT_TIMEOUT;
	struct epoll_event ev;
	struct epoll_event events[10];

	progname = strdup(argv[0]);

	while ((c = getopt(argc, argv, "g:p:f:s:t:o:h")) != -1) {
		switch (c) {
		case 'g':
			if (group) {
				free(group);
			}
			group = strdup(optarg);
			break;

		case 'p':
			port = atoi(optarg);
			break;

		case 't':
			timeoutms = atoi(optarg);
			break;

		case 'f':
			from_port = atoi(optarg);
			break;

		case 's':
			if ((cp = strrchr(optarg, ':'))) {
				*cp++ = 0;
				server_port = atoi(cp);
			}
			server = strdup(optarg);
			break;

		case 'o':
			if (outputname) {
				free(outputname);
			}
			outputname = strdup(optarg);
			break;

		default:
			printf("Error: Unrecognized option -%c\n", c);
			/* intentional fall through */
		case 'h':
			usage();
		}
	}

	if (!group) {
		group = DEFAULT_GROUP;
	}

	if (!server) {
		server = DEFAULT_SERVER;
	}

	if (!from_port) {
		srand(time(NULL));
		from_port = (rand() % 32767) + 32767;
	}

	mapreq.magic = htobe32(MXBP_MAGIC);
	mapreq.op = htobe16(MXBP_MAPREQ);
	mapreq.size = 0;
	mapreq.blockid = 0;

	if ((ctlsock = get_udp_socket(server, from_port)) < 0) {
		printf("Error:get_udp_socket(): failed.\n");
		exit(EXIT_FAILURE);
	}

	if ((epollfd = epoll_create(10)) < 0) {
		printf("%s:epoll_create(): failed: %s\n", progname,
		       strerror(errno));
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLIN;
	ev.data.fd = ctlsock;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ctlsock, &ev) == -1) {
		printf("%s:epoll_ctl(): failed: %s\n", progname,
		       strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (send_mapreq(ctlsock); !mapdesc.filesize;) {
		rv = epoll_wait(epollfd, events, 10, timeoutms);
		if (rv < 0) {
			printf("%s:epoll_wait(): failed: %s\n", progname,
			       strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (rv) {
			read_mapdesc(ctlsock);
		} else {
			/* Hit timeout, resend map request. */
			send_mapreq(ctlsock);
		}
	}

	close(epollfd);

	if (!(blockmap = calloc(1, mapdesc.nblocks))) {
		printf("Could not get %d bytes for block map: %s\n",
		       mapdesc.nblocks, strerror(errno));
		exit(EXIT_FAILURE);
	}

	cp = strrchr(recvfilename, '/');
	if (!cp) {
		cp = recvfilename;
	}

	printf("Received description: File %s length %ld blocks %d\n", cp,
	       mapdesc.filesize, mapdesc.nblocks);

	if (outputname) {
		cp = outputname;
		printf("Overriding output name with '%s'\n", cp);
	}

	/* There is no resume so we use O_TRUNC
	 * Note that we could periodically write out the blockmap
	 * and load that if present to be able to resume, but that's
	 * not a priority.
	 */
	if ((fd =
	     open(cp, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0644)) < 0) {
		printf("Could not open %s : %s\n", cp, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if ((sock = get_multicast_socket(group, port)) < 0) {
		printf("Could not get multicast socket.\n");
		exit(EXIT_FAILURE);
	}

	if ((epollfd = epoll_create(10)) < 0) {
		printf("%s:epoll_create(): failed: %s\n", progname,
		       strerror(errno));
		exit(EXIT_FAILURE);
	}
	ev.events = EPOLLIN;
	ev.data.fd = sock;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
		printf("%s:epoll_ctl(): failed: %s\n", progname,
		       strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* now just enter a read-print loop */
	while (1) {
		addrlen = sizeof(addr);
		memset(msgbuf, 0, MAX_PACKET_SIZE);

		rv = epoll_wait(epollfd, events, 10, timeoutms);
		if (rv < 0) {
			printf("%s:epoll_wait(): failed: %s\n", progname,
			       strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (rv) {

			if ((nbytes =
			     recvfrom(sock, msgbuf,
				      sizeof(mxbp_header_t) + MXBP_BLOCKSIZE, 0,
				      (struct sockaddr *)&addr,
				      &addrlen)) < 0) {
				printf("Failed to receive: %s",
				       strerror(errno));
				exit(EXIT_FAILURE);
			}

			if (nbytes < sizeof(mxbp_header_t)) {
				printf("Got short read, ignoring.\n");
				continue;
			}
			block_packet = (mxbp_packet_t *) msgbuf;
			block_packet->header.magic =
			    be32toh(block_packet->header.magic);
			if (block_packet->header.magic != MXBP_MAGIC) {
				printf("This is not an MXBP message.\n");
				continue;
			}

			block_packet->header.op =
			    be16toh(block_packet->header.op);
			if (block_packet->header.op != MXBP_BLOCK) {
				printf("Unexpected operation %d, ignoring.\n",
				       block_packet->header.op);
				continue;
			}

			block_packet->header.size =
			    be16toh(block_packet->header.size);
			block_packet->header.blockid =
			    be32toh(block_packet->header.blockid);

			if (block_packet->header.blockid > mapdesc.nblocks) {
				printf
				    ("Got Block ID %d past end of file %d, ignoring\n",
				     block_packet->header.blockid,
				     mapdesc.nblocks);
				continue;
			}

			if (!blockmap[block_packet->header.blockid]) {
				if (lseek64
				    (fd,
				     block_packet->header.blockid *
				     MXBP_BLOCKSIZE, SEEK_SET) && errno) {
					printf
					    ("Failed to seek to offset %d: %s\n",
					     block_packet->header.blockid *
					     MXBP_BLOCKSIZE, strerror(errno));
					exit(EXIT_FAILURE);
				}

				if (write
				    (fd, block_packet->data,
				     block_packet->header.size) !=
				    block_packet->header.size) {
					printf
					    ("Failed to write to offset %d: %s\n",
					     block_packet->header.blockid *
					     MXBP_BLOCKSIZE, strerror(errno));
					exit(EXIT_FAILURE);
				}

				blockmap[block_packet->header.blockid] = 1;
				if (check_finished()) {
					close(ctlsock);
					close(sock);
					close(epollfd);
					close(fd);
					printf("All done.\n");
					exit(EXIT_SUCCESS);
				}
			}
		} else {
			/* Hit the poll timeout, so send a block request. */
			send_block_req(ctlsock);
		}
	}
	exit(EXIT_SUCCESS);
}
