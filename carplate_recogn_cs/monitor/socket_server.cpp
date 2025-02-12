#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <semaphore.h>
#include "socket_server.h"
#include "capture.h"
#include "public.h"
#include "config.h"


server_info_t g_server_info;
static unsigned char tmp_databuf[PROTO_PACK_MAX_LEN];
static unsigned char tmp_protobuf[PROTO_PACK_MAX_LEN];
extern int g_send_video_flag;

extern char g_carplate_str[16];
extern int g_carplate_update;

int server_init(server_info_t *server, int port)
{
    int ret;

    memset(server, 0, sizeof(server_info_t));

    proto_lib_init();

    server->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server->fd < 0)
    {
        return -1;
    }

    server->svr_addr.sin_family = AF_INET;
    server->svr_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server->svr_addr.sin_port = htons(port);

    ret = bind(server->fd, (struct sockaddr *)&server->svr_addr, sizeof(struct sockaddr_in));
    if(ret != 0)
    {
        return -2;
    }

	// 服务器监听客户端的连接
    ret = listen(server->fd, 10);
    if(ret != 0)
    {
        return -3;
    }

    return 0;
}

int server_send_data(client_info_t *client, uint8_t *data, int len)
{
    int total = 0;
    int ret;

    if(client->fd <= 0)
    {
        printf("%s error: client not connect!\n", __FUNCTION__);
        return -1;
    }

    // lock
    pthread_mutex_lock(&client->send_mutex);
    do{
        ret = send(client->fd, data +total, len -total, 0);
        if(ret < 0)
        {
            usleep(1000);
            continue;
        }
        total += ret;
    }while(total < len);
    // unlock
    pthread_mutex_unlock(&client->send_mutex);

    return total;
}

int server_recv_data(client_info_t *client)
{
    uint8_t *tmpBuf = client->tmp_buf;
    int len, space;
    int ret = 0;

    space = ringbuf_space(&client->recv_ringbuf);

    memset(tmpBuf, 0, PROTO_PACK_MAX_LEN);
    len = recv(client->fd, tmpBuf, PROTO_PACK_MAX_LEN>space ? space:PROTO_PACK_MAX_LEN, 0);
    if(len > 0)
    {
        ret = ringbuf_write(&client->recv_ringbuf, tmpBuf, len);
    }

    return ret;
}

int __attribute__((weak)) proto_makeup_packet(unsigned char cmd, unsigned char *data, int len, unsigned char *outbuf, int size, int *outlen)
{
    unsigned char *packBuf = outbuf;
    int packLen = 0;

    packBuf[0] = 0xAA;
    packLen += 1;

    packBuf[packLen] = 'A';
    packLen += 1;

    // seq
    packBuf[packLen] = 0;
    packLen += 1;

    packBuf[packLen] = cmd;
    packLen += 1;

    memcpy(packBuf +packLen, &len, 2);
    packLen += 2;

    memcpy(packBuf +packLen, data, len);
    packLen += len;

    packBuf[packLen] = 0xFF;
    packLen += 1;

    if(size < packLen)
        return -1;

    *outlen = packLen;
    return 0;
}
int __attribute__((weak)) proto_detect_packet(void *raw_data, unsigned char *proto_data, int size, int *proto_len)
{
    struct ringbuffer *ringbuf = (struct ringbuffer *)raw_data;
    char veri_buf[] = "A";
    unsigned char buf[256];
    unsigned char byte;
    int tmp_protoLen;
    int len;

    tmp_protoLen = *proto_len;

    /* get and check protocol head */
    while(ringbuf_datalen(ringbuf) > 0)
    {
        ringbuf_read(ringbuf, &byte, 1);
        if(byte == 0xAA)
        {
            proto_data[0] = byte;
            tmp_protoLen = 1;
            //printf("********* detect head\n");
            break;
        }
    }

    /* get and check verify code */
    while(ringbuf_datalen(ringbuf) > 0)
    {
        ringbuf_read(ringbuf, &byte, 1);
        if(byte == veri_buf[tmp_protoLen-1])
        {
            proto_data[tmp_protoLen] = byte;
            tmp_protoLen ++;
            if(tmp_protoLen == 1+strlen("A"))
            {
                //printf("********* detect verify\n");
                break;
            }
        }
        else
        {
            if(byte == 0xAA)
            {
                proto_data[0] = byte;
                tmp_protoLen = 1;
            }
        }
    }

    /* get other protocol data */
    while(ringbuf_datalen(ringbuf) > 0)
    {
        // read data
        len = ringbuf_read(ringbuf, buf, tmp_protoLen);
        if(len > 0)
        {
            memcpy(proto_data +tmp_protoLen, buf, len);
            tmp_protoLen += len;

            if(proto_data[tmp_protoLen-1] != 0xFF)
            {
                printf("%s : packet data error, no detect tail!\n", __FUNCTION__);
                tmp_protoLen = 0;
                break;
            }
            *proto_len = tmp_protoLen;
            if(size < tmp_protoLen)
                return -1;
            //printf("%s : get complete protocol packet, len: %d\n", __FUNCTION__, *proto_len);
            return 0;
        }
    }

    *proto_len = tmp_protoLen;

    return -1;
}
int __attribute__((weak)) proto_analy_packet(unsigned char *pack, int packLen, unsigned char *cmd, int *len, unsigned char **data)
{
    if(packLen <= 0)
        return -1;

    //*seq = pack[2];

    *cmd = pack[3];

    memcpy(len, pack +4, 2);

    if(*len > 0)
        *data = pack + 6;

    return 0;
}

int server_0x03_heartbeat(client_info_t *client, uint8_t *data, int len, uint8_t *ack_data, int size, int *ack_len)
{
    uint32_t tmpTime;
    int tmplen = 0;
    int ret;

    UNUSED_3(client,len,size);

    /* request part */
    memcpy(&tmpTime, data, 4);
    //printf("%s: time: %ld\n", __FUNCTION__, tmpTime);

    /* ack part */
    ret = 0;
    memcpy(ack_data +tmplen, &ret, 4);
    tmplen += 4;

    tmpTime = (uint32_t)time(NULL);
    memcpy(ack_data +tmplen, &tmpTime, 4);
    tmplen += 4;

    *ack_len = tmplen;

    return 0;
}

int server_0x10_carplate(client_info_t *client, uint8_t *data, int len, uint8_t *ack_data, int size, int *ack_len)
{
    UNUSED_4(client,len,ack_data,size);

    strncpy(g_carplate_str, (const char *)data, 16);
    printf("%s: carplate: %s\n", __FUNCTION__, g_carplate_str);

    g_carplate_update = 1;

    *ack_len = 0;

    return 0;
}

int server_0x20_switchcamera(client_info_t *client, uint8_t *data, int len, uint8_t *ack_data, int size, int *ack_len)
{
    unsigned char onoff = 0;

    UNUSED_4(client,len,ack_data,size);

    onoff = data[0];
    printf("%s: onoff=%d\n", __FUNCTION__, onoff);

    g_send_video_flag = onoff;

    *ack_len = 0;

    return 0;
}

int proto_0x21_sendframe(unsigned char format, void *frame, int len)
{
    client_info_t *client = &g_server_info.client;
    int offset = 0;
    int pack_len = 0;

    /* format */
    tmp_databuf[offset] = format;
    offset += 1;

    /* data len */
    memcpy(tmp_databuf +offset, &len, 4);
    offset += 4;

    /* frame data */
    memcpy(tmp_databuf +offset, frame, len);
    offset += len;

    proto_makeup_packet(0x21, tmp_databuf, offset, tmp_protobuf, sizeof (tmp_protobuf), &pack_len);

    return server_send_data(client, tmp_protobuf, pack_len);
}


int server_proto_handle(client_info_t *client, unsigned char *pack, unsigned int pack_len)
{
    uint8_t *ack_buf = client->ack_buf;
    uint8_t *tmpBuf = client->tmp_buf;
    uint8_t cmd = 0;
    int data_len = 0;
    uint8_t *data = 0;
    int ack_len = 0;
    int tmpLen = 0;
    int ret;

    ret = proto_analy_packet(pack, pack_len, &cmd, &data_len, &data);
    if(ret != 0)
        return -1;

    //printf("%s: recv cmd: 0x%02x, seq: %d, pack_len: %d, data_len: %d\n", __FUNCTION__, cmd, seq, pack_len, data_len);

    switch(cmd)
    {
        case 0x01:
            break;

        case 0x03:
            ret = server_0x03_heartbeat(client, data, data_len, ack_buf, PROTO_PACK_MAX_LEN, &ack_len);
            break;

        case 0x10:
            ret = server_0x10_carplate(client, data, data_len, ack_buf, PROTO_PACK_MAX_LEN, &ack_len);
            break;

        case 0x20:
            ret = server_0x20_switchcamera(client, data, data_len, ack_buf, PROTO_PACK_MAX_LEN, &ack_len);
            break;

    default:
            break;
    }

    /* send ack data */
    if(ret==0 && ack_len>0)
    {
        proto_makeup_packet(cmd, ack_buf, ack_len, tmpBuf, PROTO_PACK_MAX_LEN, &tmpLen);
        server_send_data(client, tmpBuf, tmpLen);
    }

    return 0;
}

void *server_task_thread(void *arg)
{
    client_info_t *client = (client_info_t *)arg;
    int recv_ret;
    int det_ret;
    int flags;
    int ret;

    pthread_mutex_init(&client->send_mutex, NULL);

    flags = fcntl(client->fd, F_GETFL, 0);
    fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);

    ret = ringbuf_init(&client->recv_ringbuf, SVR_RECVBUF_SIZE);
    if(ret != 0)
        return NULL;

    while(1)
    {
        recv_ret = server_recv_data(client);
        det_ret = proto_detect_packet(&client->recv_ringbuf, client->proto_buf, sizeof(client->proto_buf), &client->proto_len);
        if(det_ret == 0)
        {
            server_proto_handle(client, client->proto_buf, client->proto_len);
        }

        if(recv_ret<=0 && det_ret!=0)
        {
            usleep(30*1000);
        }
    }

    return NULL;
}

void *server_listen_thread(void *arg)
{
    server_info_t *server = &g_server_info;
    struct sockaddr_in client_addr;
    pthread_t tid;
    int len;
    int ret;

    UNUSED_1(arg);

    ret = server_init(server, DEFAULT_SERVER_PORT);
    if(ret != 0)
    {
        printf("%s server init failed! ret: %d\n", __FUNCTION__, ret);
        return NULL;
    }

    while(1)
    {
        memset(&client_addr, 0, sizeof(struct sockaddr_in));

		// 接受客户端发起的连接
        server->client.fd = accept(server->fd, (struct sockaddr *)&server->client.sockaddr, (socklen_t *)&len);
        if(server->client.fd < 0)
        {
            continue;
        }
        printf("********** accept client tcp connect ok **********\n");

        ret = pthread_create(&tid, NULL, server_task_thread, &server->client);
        if(ret != 0)
        {
            printf("create server thread failed!\n");
        }
    }

    return NULL;
}

int start_socket_server_task(void)
{
    pthread_t tid;
    int ret;

    ret = pthread_create(&tid, NULL, server_listen_thread, NULL);
    if(ret != 0)
    {
        return -1;
    }

    return 0;
}

#ifdef PROTO_LIB_INIT
int proto_lib_init(void)
{
    memset(tmp_databuf, 0, PROTO_PACK_MAX_LEN);
    memset(tmp_protobuf, 0, PROTO_PACK_MAX_LEN);

    printf("%s %d: ok\n", __FUNCTION__, __LINE__);
    return 0;
}
#endif
