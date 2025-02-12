#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include <pthread.h>
#include <netinet/in.h>
#include "ringbuffer.h"

#define PROTO_PACK_MAX_LEN		(1 *1024 *1024)
#define PROTO_PACK_MIN_LEN		(12)

#define SVR_RECVBUF_SIZE			(PROTO_PACK_MAX_LEN*6)
#define SVR_SENDBUF_SIZE			PROTO_PACK_MAX_LEN

typedef struct {
    int fd;
    pthread_mutex_t	send_mutex;
    struct sockaddr_in sockaddr;
    struct ringbuffer recv_ringbuf;			// socket receive data ring buffer
    unsigned char tmp_buf[PROTO_PACK_MAX_LEN];
    unsigned char proto_buf[PROTO_PACK_MAX_LEN];		// protocol packet data buffer
    int proto_len;
    unsigned char ack_buf[PROTO_PACK_MAX_LEN];
} client_info_t;

typedef struct {
    int fd;
    struct sockaddr_in 	svr_addr;		// server ip addr
    client_info_t client;
} server_info_t;

int proto_makeup_packet(unsigned char cmd, unsigned char *data, int len, unsigned char *outbuf, int size, int *outlen);

/*
 * raw_data: must be "struct ringbuffer" type
 */
int proto_detect_packet(void *raw_data, unsigned char *proto_data, int size, int *proto_len);

int proto_analy_packet(unsigned char *pack, int packLen, unsigned char *cmd, int *len, unsigned char **data);

int proto_lib_init(void);

int proto_0x21_sendframe(unsigned char format, void *frame, int len);

int start_socket_server_task(void);


#endif // SOCKET_SERVER_H
