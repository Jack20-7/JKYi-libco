#include "co_routine.h"

#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <string>

#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define         MAX_CLIENT_NUM           1000000
#define         TIME_SUB_MS(tv1,tv2)     ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

#define         WEBSOCKET_SERVER_PORT    8000
#define         GUID                     "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define         MAX_BUFFER_LENGTH        1024

//websocket协议的首部
struct opthdr{
    unsigned char opcode: 4,
                  rsv3  : 1,
                  rsv2  : 1,
                  rsv1  : 1,
                  fin   : 1;
    unsigned char payload_length: 7,
                  mask          : 1;
}__attribute__((packed));

struct websocket_head_126{
    unsigned short payload_length;
    char           mask_key[4];
    unsigned char  data[8];
}__attribute__((packed));

struct websocket_head_127{
    unsigned long long payload_length;
    char               mask_key[4];
    unsigned char      data[8];
}__attribute__((packed));


#define UNION_HEADER(type1,type2)     \
         struct {                     \
             struct type1 hdr;        \
             struct type2 len;        \
         }                            

typedef struct websocket_head_127 websocket_head_127;
typedef struct websocket_head_126 websocket_head_126;
typedef struct opthdr             opthdr;


static int SetNonblock(int fd){
    int flags;
    flags = fcntl(fd,F_GETFL,0);
    if(flags < 0){
        return flags;
    }
    flags |= O_NONBLOCK;
    if(fcntl(fd,F_SETFL,flags) < 0){
        return -1;
    }

    return 0;
}

static int SetBlock(int fd){
    int flags = fcntl(fd,F_GETFL,0);
    if(flags < 0){
        return flags;
    }

    flags &= ~O_NONBLOCK;
    if(fcntl(fd,F_SETFL,flags) < 0){
        return -1;
    }
    return 0;
}

int base64_encode(char* in_str,int in_len,char* out_str){
    BIO* b64,* bio;
    BUF_MEM* bptr = NULL;
    size_t size = 0;

    if(in_str == NULL || out_str == NULL){
        return -1;
    }

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64,bio);

    BIO_write(bio,in_str,in_len);
    BIO_flush(bio);

    BIO_get_mem_ptr(bio,&bptr);
    memcpy(out_str,bptr->data,bptr->length);
    out_str[bptr->length - 1] = '\0';
    size = bptr->length;

    BIO_free_all(bio);
    return size;
}

//按行读取数据
int readline(char* allbuf,int level,char* linebuf){
    int len = strlen(allbuf);
    for(;level < len;++level){
        if(allbuf[level] == '\r' && allbuf[level + 1] == '\n'){
            return level + 2;
        }else{
            *(linebuf++) = allbuf[level];
        }
    }
    return -1;
}

void umask(char* data,int len,char* mask){
    int i = 0;
    for(i = 0;i < len;++i){
        *(data + i) ^= *(mask + (i % 4));
    }
}

//对websocket报文进行解析
char* decode_packet(char* stream,char* mask,int length,int* ret){
    opthdr* hdr = (opthdr*)stream;
    unsigned char* data = reinterpret_cast<unsigned char*>(stream) + sizeof(opthdr);
    int size = 0;
    int start = 0;
    int i = 0;

    if((hdr->mask & 0x7f) == 126){
        websocket_head_126* hdr126 = (websocket_head_126*)data;
        size = hdr126->payload_length;

        for(i = 0;i < 4;++i){
            mask[i] = hdr126->mask_key[i];
        }
        start = 8;
    }else if((hdr->mask & 0x7f) == 127){
        websocket_head_127* hdr127 = (websocket_head_127*)data;
        size = hdr127->payload_length;
        for(i = 0;i < 4;++i){
            mask[i] = hdr127->mask_key[i];
        }
        start = 14;
    }else{
        size = hdr->payload_length;

        memcpy(mask,data,4);
        start = 6;
    }

    *ret = size;
    //对负载数据进行解密
    umask(stream + start,size,mask);

    return stream + start;
}
//生成要发送的websocket报文
int encode_packet(char* buffer,char* mask,char* stream,int length){
    opthdr head = {0};
    head.fin = 1;       //单个单个发送
    head.opcode = 1;
    int size = 0;

    if(length < 126){
        head.payload_length = length;
        memcpy(buffer,&head,sizeof(opthdr));
        size = 2;
    }else if(length < 0xffff){
        websocket_head_126 hdr = {0};
        hdr.payload_length = length;
        memcpy(hdr.mask_key,mask,4);

        memcpy(buffer,&head,sizeof(opthdr));
        memcpy(buffer + sizeof(opthdr),&hdr,sizeof(websocket_head_126));
        size = sizeof(websocket_head_126);
    }else{
        websocket_head_127 hdr = {0};
        hdr.payload_length = length;
        memcpy(hdr.mask_key,mask,4);

        memcpy(buffer,&head,sizeof(opthdr));
        memcpy(buffer + sizeof(opthdr),&hdr,sizeof(websocket_head_127));

        size = sizeof(websocket_head_127);
    }

    //printf("buffer = %s\n",buffer);
    memcpy(buffer + 2,stream,length);

    return length + 2;
}

void* server_reader(void* arg){
    int fd = *(int*)arg;
    int length = 0;
    int ret = 0;

    while(1){
        char stream[MAX_BUFFER_LENGTH] = {0};
        length = read(fd,stream,MAX_BUFFER_LENGTH);

        if(length){
            if(fd > MAX_CLIENT_NUM){
                 printf("read from server:%.*s\n",length,stream);
            }
            char mask[4] = {0};
            char* data = decode_packet(stream,mask,length,&ret); //解析收到的websocket报文

            //printf("data:%s,length:%d\n",data,ret);

            char buffer[MAX_BUFFER_LENGTH + 14] = {0};
            ret = encode_packet(buffer,mask,data,ret);           //基于websocket协议实现的echo
            ret = write(fd,buffer,ret);
        }else if(length == 0){
            close(fd);
            break;
        }
    }
    return NULL;
}

int handshake(int cli_fd){
    //完成websocket的握手流程
    int level = 0;
    char buffer[MAX_BUFFER_LENGTH];
    char linebuf[128];
    char sec_accept[32] = {0};
    unsigned char sha1_data[SHA_DIGEST_LENGTH + 1] = {0};

    if(read(cli_fd,buffer,sizeof(buffer)) <= 0){
        perror("read");
    }

    printf("request:\n");
    printf("%s\n",buffer);

    do{
        memset(linebuf,0,sizeof(linebuf));
        level = readline(buffer,level,linebuf);

        if(strstr(linebuf,"Sec-WebSocket-Key") != NULL){
            strcat(linebuf,GUID);

            SHA1((unsigned char*)&linebuf + 19,strlen(linebuf + 19),(unsigned char*)&sha1_data);

            memset(buffer,0,MAX_BUFFER_LENGTH);

            //C++中不允许向strlen传入unsigned char* 类型的参数
            std::string str((char*)sha1_data);
            base64_encode(const_cast<char*>(str.c_str()),str.size(),sec_accept);
            sprintf(buffer,"HTTP/1.1 101 Switching Protocols\r\n" \
                            "Upgrade: websocket\r\n" \
                            "Connection: Upgrade\r\n" \
                            "Sec-WebSocket-Accept:%s\r\n" \
                            "\r\n",sec_accept);
            printf("response\n");
            printf("%s",buffer);

            if(write(cli_fd,buffer,strlen(buffer)) < 0){
                perror("write");
            }
            break;
        }
    }while((buffer[level] != '\r' || buffer[level + 1] != '\n') && level != -1);
    return 0;
}

int init_server(){
    int sockfd = socket(PF_INET,SOCK_STREAM,0);
    if(sockfd < 0){
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(WEBSOCKET_SERVER_PORT);

    if(bind(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in)) < 0){
        printf("bind error\n");
        return -2;
    }

    if(listen(sockfd,5) < 0){
        printf("listen error\n");
        return -2;
    }

    return sockfd;
}

void* server(void* arg){
    int sockfd = init_server();

    struct timeval tv_begin;
    gettimeofday(&tv_begin,NULL);
    while(1){
        socklen_t len = sizeof(struct sockaddr_in);
        struct sockaddr_in remote;

        int cli_fd = co_accept(sockfd,(struct sockaddr*)&remote,&len);
        if(cli_fd % 1000 == 999){
            struct timeval tv_cur;
            memcpy(&tv_cur,&tv_begin,sizeof(struct timeval));

            gettimeofday(&tv_begin,NULL);
            int time_used = TIME_SUB_MS(tv_begin,tv_cur);

            printf("client fd:%d,time_used:%d\n",cli_fd,time_used);
        }

        printf("new client comming\n");
        //SetBlock(cli_fd);
        handshake(cli_fd);
        //SetNonblock(cli_fd);

        //完成握手，接下来就创建一个协程专门的为该连接进行服务
        stCoRoutine_t* co = NULL;
        co_create(&co,NULL,server_reader,&cli_fd);
        co_resume(co);
    }
}

int main(int argc,char** argv){
    stCoRoutine_t* co = NULL;
    co_create(&co,NULL,server,NULL);
    co_resume(co);
    co_eventloop(co_get_epoll_ct(),NULL,NULL);
    return 0;
}