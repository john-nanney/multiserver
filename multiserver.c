
#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "get_socket.h"
#include "mxb_protocol.h"

typedef struct blockdesc_t {
	uint64_t first;
	uint64_t last;
	struct blockdesc_t *next;
} blockdesc_t;

static int filefd;

static char *filename = NULL;
static char *group = NULL;
static uint16_t port = DEFAULT_PORT;

static mxbp_header_t map_packet_header;
static mxbp_map_t map_packet_payload;
static char *map_filename = NULL;
static struct iovec mapvec[3];
static struct msghdr mapmsg;

static char *server = NULL;
static uint16_t server_port = DEFAULT_SERVER_PORT;

static blockdesc_t *qtop = NULL;
static pthread_mutex_t wqlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wqready = PTHREAD_COND_INITIALIZER;

static uint32_t ipdelay = 0;

static char bdbuf[MAX_PACKET_SIZE];

static int timeoutms = 100;

static char *progname = NULL;

void usage(void) {
	printf("\nUsage: %s -f FILENAME [ -p PORT ] [ -g GROUP ] [ -s SERVER_PORT ] [ -d INTERPACKET_DELAY ]\n\n", progname?progname:"multiserver");
	printf("FILENAME is the file to serve.\n");
	printf("PORT is the multicast port to serve on, defaults to %d\n", DEFAULT_PORT);
	printf("GROUP is the multicast address, defaults to \"%s\"\n", DEFAULT_GROUP);
	printf("SERVER_PORT is the block map server port, defaults to %d\n", DEFAULT_SERVER_PORT);
	printf("INTERPACKET_DELAY is the delay in milliseconds between multicast packets. This is only for debugging, never actually use it.\n");
	puts("\n");
	exit(EXIT_FAILURE);
}

int64_t get_file_size(const char *filename)
{
	struct stat s;

	if(stat(filename, &s)) {
		printf("%s:stat(): failed: %s\n", progname, strerror(errno));
		return((int64_t)-1);
	}
	return(s.st_size);
}

/* Cache the map packet and an iovec list describing it, because it never changes. */
void setup_map_packet(uint64_t filesize, uint32_t blocksize, const char *filename)
{
	map_packet_payload.filesize = htobe64(filesize);
	map_packet_payload.blocksize = htobe32(blocksize);
	/* Trailer block if uneven. */
	map_packet_payload.nblocks = htobe32((filesize/blocksize) + (filesize%blocksize?1:0));

	map_filename = strdup(filename);

	map_packet_header.magic = htobe32(MXBP_MAGIC);
	map_packet_header.op = htobe16(MXBP_BLOCKDESC);
	/* extra 1 for terminating NULL */
	map_packet_header.size = htobe16((uint16_t)(sizeof(map_packet_payload) + strlen(filename) + 1));
	map_packet_header.blockid = 0;

	mapvec[0].iov_base = &map_packet_header;
	mapvec[0].iov_len = sizeof(map_packet_header);
	mapvec[1].iov_base = &map_packet_payload;
	mapvec[1].iov_len = sizeof(map_packet_payload);
	mapvec[2].iov_base = map_filename;
	mapvec[2].iov_len = strlen(map_filename) + 1;

	memset(&mapmsg, 0, sizeof(mapmsg));
	mapmsg.msg_iov = mapvec;
	mapmsg.msg_iovlen = 3;
}

void *control_thread(void *arg)
{
	int ctlsock;
	int epollfd;
	int rv;
	uint32_t firstblock = 0;
	uint32_t lastblock = 0;
	ssize_t bytes;
	blockdesc_t *nextblockreq;
	blockdesc_t *curr;
	socklen_t addrlen;
	struct epoll_event ev;
	struct epoll_event events[10];
	struct sockaddr_in src_addr;
	mxbp_packet_t *packet;
	mxbp_header_t header;
	uint32_t *uip;

	if((ctlsock = get_udp_socket(server, server_port)) < 0) {
		printf("Could not get control socket %s:%d %s\n", server, server_port, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if((epollfd = epoll_create(10)) < 0) {
		printf("%s:epoll_create(): failed: %s\n", progname, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLIN;
	ev.data.fd = ctlsock;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ctlsock, &ev) == -1) {
		printf("%s:epoll_ctl(): failed: %s\n", progname, strerror(errno));
		exit(EXIT_FAILURE);
	}

	while(1) {
		rv = epoll_wait(epollfd, events, 10, timeoutms);
		if(rv < 0) {
			printf("%s:epoll_wait(): failed: %s\n", progname, strerror(errno));
			exit(EXIT_FAILURE);
		}
		if(rv) {
			addrlen = sizeof(src_addr);
			memset(&header, 0, sizeof(header));
			bytes = recvfrom(ctlsock, &bdbuf, MAX_PACKET_SIZE, 0, (struct sockaddr*)&src_addr, &addrlen);

			packet = (mxbp_packet_t*)bdbuf;

			if(bytes == -1) {
				printf("Control socket error: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}

			if(bytes < sizeof(header)) {
				/* Could make this more resilient... */
				continue;
			}

			printf("Received %ld bytes\n", bytes);

			packet->header.magic = be32toh(packet->header.magic);
			if(packet->header.magic != MXBP_MAGIC) {
				printf("Not a valid header, wanted 0x%08x got 0x%08x\n", MXBP_MAGIC, packet->header.magic);
				continue;
			}

			packet->header.op = be16toh(packet->header.op);
			packet->header.size = be16toh(packet->header.size);
			/* the control channel doesn't care about blockid */

			switch(packet->header.op) {
				case MXBP_MAPREQ:
					mapmsg.msg_name = &src_addr;
					mapmsg.msg_namelen = addrlen;
					if(sendmsg(ctlsock, &mapmsg, 0) < 0) {
						printf("sendmsg(): failed sending map message: %s\n", strerror(errno));
						exit(EXIT_FAILURE);
					}
					break;

				case MXBP_BLOCKREQ:
					if(packet->header.size < sizeof(uint32_t)*2) {
						printf("Got block request but not enough bytes to specify a range.\n");
						continue;
					}

					/* Ugh, since we're getting only a single range from the client, we can get away with this. */
					uip = (uint32_t*)packet->data;
					firstblock = be32toh(uip[0]);
					lastblock = be32toh(uip[1]);

					if(!(nextblockreq = malloc(sizeof(blockdesc_t)))) {
						printf("Memory allocation failed for %ld bytes\n", sizeof(blockdesc_t));
						exit(EXIT_FAILURE);
					}
					nextblockreq->first = firstblock;
					nextblockreq->last = lastblock;

					if(firstblock > lastblock) {
						printf("Mangled block request: start %d end %d, ignoring.\n", firstblock, lastblock);
						while(recvfrom(ctlsock, &bdbuf, MAX_PACKET_SIZE, 0, (struct sockaddr*)&src_addr, &addrlen) > 0) {
							/* drain the control socket */
						}
						pthread_mutex_unlock(&wqlock);
						continue;
					}

					printf("Pushing range %d to %d onto the block range list.\n", firstblock, lastblock);

					pthread_mutex_lock(&wqlock);
					if(!qtop) {
						qtop = nextblockreq;
					} else {
						for(curr = qtop; curr->next; curr = curr->next) {
							/* find end of chain. */
						}
						curr->next = nextblockreq;
					}
					pthread_mutex_unlock(&wqlock);
					printf("DEBUG: Signalling mcast thread.\n");
					pthread_cond_signal(&wqready);
					break;
				default:
					printf("Unrecognized operation in control server: %d\n", packet->header.op);
			} /* end switch(packet->header.op) */
		} /* end if(rv) */
	} /* end control server loop */
}

void data_server_thread(void)
{
	struct sockaddr_in addr;
	struct timespec ts;
	struct timespec ipdts;
	int sock;
	int x;
	blockdesc_t *freeme;
	char *block_packet_buffer;
	mxbp_packet_t *block_packet;
	off64_t bytes;
	uint32_t start;
	uint32_t end;

	if(!(block_packet_buffer = malloc(sizeof(mxbp_header_t) + MXBP_BLOCKSIZE))) {
		printf("Could not allocate transmit buffer %ld bytes: %s\n", sizeof(mxbp_header_t) + MXBP_BLOCKSIZE, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* create what looks like an ordinary UDP socket */
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("%s:socket(): Can't get UDP socket: %s\n", progname, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* set up destination address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(group);
	addr.sin_port = htons(port);

	/* now just sendto() our destination */
	while (1) {
		pthread_mutex_lock(&wqlock);
		if(!qtop) {
			ts.tv_sec = 0;
			ts.tv_nsec = 10000; /* 10 milliseconds */
			errno = 0;
			printf("DEBUG: Server waiting on work queue.\n");
			if(pthread_cond_timedwait(&wqready, &wqlock, &ts)) {
				if(errno && errno != ETIMEDOUT) {
					printf("Waiting for work items failed: %s (%d)\n", strerror(errno), errno);
					exit(EXIT_FAILURE);
				}
				pthread_mutex_unlock(&wqlock);
				continue;
			}
		}

		start = qtop->first;
		end = qtop->last;
		freeme = qtop;
		qtop = qtop->next;
		free(freeme); /* This is before the unlock on purpose... */
		pthread_mutex_unlock(&wqlock);

		if(end < start) {
			printf("Mangled block request: start %d end %d, ignoring.\n", start, end);
			continue;
		}

		printf("Serving range %d to %d\n", start, end);

		block_packet = (mxbp_packet_t*)block_packet_buffer;
		block_packet->header.magic = htobe32(MXBP_MAGIC);
		block_packet->header.op = htobe16((uint16_t)MXBP_BLOCK);

		if(start) {
			if(lseek64(filefd, (uint64_t)start * MXBP_BLOCKSIZE, SEEK_SET) == -1) {
				printf("Could not seek to %d: %s\n", start, strerror(errno));
				exit(EXIT_FAILURE);
			}
		} else {
			lseek64(filefd, 0UL, SEEK_SET);
		}
		for(x = start; x <= end; ++x) {
			bytes = read(filefd, block_packet->data, MXBP_BLOCKSIZE);
			if(bytes == -1) {
				printf("Error reading file: %s\n", strerror(errno));
			}
			block_packet->header.size = htobe16(bytes);
			block_packet->header.blockid = htobe32(x);
			if (sendto(sock, block_packet, sizeof(block_packet->header) + bytes, 0, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
				printf("Error multicasting block: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			if(ipdelay) {
				ipdts.tv_sec = 0;
				ipdts.tv_nsec = ipdelay;
				nanosleep(&ipdts, NULL);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	int c;
	char *cp;
	uint64_t filesize;
	pthread_t control_thread_id;

	progname = strdup(argv[0]);

	while((c = getopt(argc, argv, "f:g:p:s:d:h")) != -1) {
		switch(c) {
			case 'f':
				if(filename) {
					free(filename);
				}
				filename = strdup(optarg);
				break;

			case 'g':
				if(group) {
					free(group);
				}
				group = strdup(optarg);
				break;

			case 's':
				if((cp = strrchr(optarg, ':'))) {
					*cp++ = 0;
					server_port = atoi(cp);
				}
				server = strdup(optarg);
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 'd':
				/* HOLY PHRACKING CRAPBALLS! This slows it down horribly, even for one ns. */
				ipdelay = atoi(optarg);
				break;

			default:
				printf("Error: Unrecognized option -%c\n", c);
			case 'h':
				usage();
		}
	}

	if(!filename) {
		printf("Error: You must specify at least a filename.\n");
		usage();
	}

	if((filesize = get_file_size(filename)) < 1) {
		printf("Can't get file size.\n");
		exit(EXIT_FAILURE);
	}

	setup_map_packet(filesize, MXBP_BLOCKSIZE, filename);

	if((filefd = open(filename, O_RDONLY | O_LARGEFILE)) < 0) {
		printf("Cannot open %s : %s\n", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(!group) {
		group = DEFAULT_GROUP;
	}

	if(pthread_create(&control_thread_id, NULL, control_thread, NULL)) {
		printf("Could not create control server thread: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Server entering multicast loop for file %s\n", filename);
	data_server_thread();

	exit(EXIT_SUCCESS);
}
