#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "socket_client.h"
#include "public.h"
#include "ringbuffer.h"
#include "config.h"

char g_server_ip[16];
client_info_t g_client_info;
static unsigned char tmp_databuf[PROTO_PACK_MAX_LEN];
static unsigned char tmp_protobuf[PROTO_PACK_MAX_LEN];
extern int g_send_video_flag;

int client_init(client_info_t *client, char *srv_ip, int srv_port)
{
    int flags = 0;
    int ret;
	
    printf("server: %s, port: %d\n", srv_ip, srv_port);

    memset(client, 0, sizeof(client_info_t));

    proto_lib_init();

    client->tcp_state = TCP_STATE_DISCONNECT;

	// 创建一个socket套接字,TCP类型
    client->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(client->fd < 0)
    {
        return -1;
    }

    pthread_mutex_init(&client->send_mutex, NULL);

	// 设置参数属性
    flags = fcntl(client->fd, F_GETFL, 0);
    fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);

    client->svr_addr.sin_family = AF_INET;
    inet_pton(AF_INET, srv_ip, &client->svr_addr.sin_addr);
    client->svr_addr.sin_port = htons(srv_port);

    ret = ringbuf_init(&client->recv_ringbuf, CLIENT_SENDBUF_SIZE);
    if(ret != 0)
    {
        return -2;
    }

    return 0;
}

int client_send_data(client_info_t *client, unsigned char *data, int len)
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
		// 发送数据
        ret = send(client->fd, data +total, len -total, 0);
        if(ret < 0)
        {
            if(errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN)
            {
                usleep(1000);
                continue;
            }
            else
            {
                client->tcp_state = TCP_STATE_CLOSE;
                perror("socket send failed");
                printf("ret: %d, errno = %d\n", ret, errno);
                break;
            }
        }
        total += ret;
    }while(total < len);
    // unlock
    pthread_mutex_unlock(&client->send_mutex);

    return total;
}

int client_recv_data(client_info_t *client)
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
    else if(len < 0)
    {
        if(errno!=EINTR && errno!=EWOULDBLOCK && errno!=EAGAIN)
        {
            client->tcp_state = TCP_STATE_CLOSE;
            perror("socket recv failed");
            printf("ret: %d, errno = %d\n", len, errno);
        }
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

int proto_0x03_heartbeat(client_info_t *client)
{
    uint8_t proto_buf[128];
    int pack_len = 0;
    uint32_t time_now;
    int data_len = 0;

    time_now = (uint32_t)time(NULL);
    data_len += 4;

    proto_makeup_packet(0x03, (uint8_t *)&time_now, data_len, proto_buf, sizeof (proto_buf), &pack_len);

    client_send_data(client, proto_buf, pack_len);

    return 0;
}

int client_0x03_heartbeat(client_info_t *client, uint8_t *data, int len, uint8_t *ack_data, int size, int *ack_len)
{
    uint32_t time;
    int offset = 0;
    int ret;

    UNUSED_4(client,len,ack_data,size);

    memcpy(&ret, data +offset, 4);
    offset += 4;

    /* bejing time */
    memcpy(&time, data +offset, 4);
    offset += 4;

    printf("%s: ret %d, time: %d\n", __FUNCTION__, ret, time);

    if(ack_len != NULL)
        *ack_len = 0;

    return 0;
}

int proto_0x10_carplate(char *plate)
{
    uint8_t proto_buf[128];
    int pack_len = 0;

    printf("carplate: %s\n", plate);

    proto_makeup_packet(0x10, (unsigned char *)plate, 16, proto_buf, sizeof(proto_buf), &pack_len);

    return client_send_data(&g_client_info, proto_buf, pack_len);
}

int proto_0x20_switchcamera(unsigned char onoff)
{
    uint8_t proto_buf[128];
    int pack_len = 0;

    printf("switch camera: %d\n", onoff);

    proto_makeup_packet(0x20, &onoff, 1, proto_buf, sizeof(proto_buf), &pack_len);

    return client_send_data(&g_client_info, proto_buf, pack_len);
}

int server_0x21_recvframe(client_info_t *client, uint8_t *data, int len, uint8_t *ack_data, int size, int *ack_len)
{
    uint8_t type = 0;
    int frame_len = 0;
    uint8_t *frame = NULL;
    int offset = 0;

    UNUSED_4(client,len,ack_data,size);

    type = data[offset];
    offset += 1;
    (void)type;

    memcpy(&frame_len, data+offset, 4);
    offset += 4;

    frame = data + offset;
    offset += frame_len;

    //printf("*** recv one frame data: type: %d, data_len: %d\n", type, frame_len);

    /* put frame to detect */
    v4l2cap_update_newframe(frame, frame_len);

    *ack_len = 0;

    return 0;
}

int client_proto_handle(client_info_t *client, unsigned char *pack, unsigned int pack_len)
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
            ret = client_0x03_heartbeat(client, data, data_len, ack_buf, PROTO_PACK_MAX_LEN, &ack_len);
            break;

        case 0x21:
            ret = server_0x21_recvframe(client, data, data_len, ack_buf, PROTO_PACK_MAX_LEN, &ack_len);
            break;

        default:
            break;
    }

    /* send ack data */
    if(ret==0 && ack_len>0)
    {
        proto_makeup_packet(cmd, ack_buf, ack_len, tmpBuf, PROTO_PACK_MAX_LEN, &tmpLen);
        client_send_data(client, tmpBuf, tmpLen);
    }

    return 0;
}

void *client_task_thread(void *arg)
{
    client_info_t *client = &g_client_info;
    time_t heartbeat_time = 0;
    time_t tmp_time;
    int ret;

    UNUSED_1(arg);

	// 初始化客户端，创建socket，设置相关属性
    ret = client_init(client, g_server_ip, DEFAULT_SERVER_PORT);
    if(ret != 0)
    {
        printf("%s client init failed!\n", __FUNCTION__);
        return NULL;
    }

    while(client->tcp_state != TCP_STATE_CLOSE)
    {
        switch (client->tcp_state)
        {
            case TCP_STATE_DISCONNECT:
				// 连接服务器
                ret = connect(client->fd, (struct sockaddr *)&client->svr_addr, sizeof(struct sockaddr_in));
                if(ret == 0)
                {
                    client->tcp_state = TCP_STATE_CONNECTED;
                    printf("********** tcp connect server ok **********\n");
                }
                break;

            case TCP_STATE_CONNECTED:
                // 保持心跳
                tmp_time = time(NULL);
                if(abs(tmp_time - heartbeat_time) >= 6)
                {
                    proto_0x03_heartbeat(client);
                    heartbeat_time = tmp_time;
                }
                break;

            case TCP_STATE_LOGIN_OK:    // not use
                break;

            default:
                break;
        }

        if(client->tcp_state == TCP_STATE_CONNECTED)
        {
            int recv_ret;
            int det_ret;

            recv_ret = client_recv_data(client);
            det_ret = proto_detect_packet(&client->recv_ringbuf, client->proto_buf, sizeof(client->proto_buf), &client->proto_len);
            if(det_ret == 0)
            {
                client_proto_handle(client, client->proto_buf, client->proto_len);
            }

            if(recv_ret<=0 && det_ret!=0)
            {
                usleep(30*1000);
            }
        }
        else
        {
            usleep(100*1000);
        }
    }

    return NULL;
}

int start_socket_client_task(char *svr_ip)
{
    pthread_t tid;
    int ret;

    memset(g_server_ip, 0, sizeof(g_server_ip));
    if(svr_ip)
        strncpy(g_server_ip, svr_ip, sizeof(g_server_ip));
    else
        strncpy(g_server_ip, DEFAULT_SERVER_IP, sizeof(DEFAULT_SERVER_IP));

    ret = pthread_create(&tid, NULL, client_task_thread, NULL);
    if(ret != 0)
    {
        return -1;
    }

    return 0;
}

int stop_socket_client_task(void)
{
    client_info_t *client = &g_client_info;

    client->tcp_state = TCP_STATE_CLOSE;

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
