
#ifdef __GNUC__
#pragma once
#endif /* __GNUC__ */

#ifndef _MXB_PROTOCOL_H
#define _MXB_PROTOCOL_H (1)

#include <stdint.h>

#define DEFAULT_PORT (16000)
#define DEFAULT_GROUP "225.0.20.10"
#define DEFAULT_TIMEOUT (1000)
#define DEFAULT_SERVER "192.168.1.1"
#define DEFAULT_SERVER_PORT (7500)

#define MXBP_MAGIC (0x4d584250)
#define MXBP_BLOCKSIZE (1024)
#define MAX_PACKET_SIZE (2048)
#define FILENAMESIZE (2048)

/* Packet header */
typedef struct {
    uint32_t magic;
    uint16_t op;
    uint16_t size;
    uint32_t blockid;
} mxbp_header_t;

/* Packet structure */
typedef struct {
    mxbp_header_t header;
    uint8_t data[1];
} mxbp_packet_t;

/* Info contained in map packet */
typedef struct {
    uint64_t filesize;
    uint32_t blocksize;
    uint32_t nblocks;
} mxbp_map_t;

/* Packed identifier */
typedef enum {
	/* must be first */
    MXBP_INVALID = 0,

	/* Client requesting file info */
    MXBP_MAPREQ,

	/* File info from server */
    MXBP_BLOCKDESC,

	/* Client requesting range of blocks */
    MXBP_BLOCKREQ,

	/* Block data */
    MXBP_BLOCK,

	/* must be last */
    MXBP_LAST
} mxbp_op_t;

#endif /* _MXB_PROTOCOL_H */
