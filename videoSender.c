/* 映像送信プログラム */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <pcap.h>

#include <arpa/inet.h>
#include <time.h>

#include <unistd.h> /* for close()に必要 */
#include <fcntl.h>   // O_RDONLY等
#include <errno.h>
#include <limits.h>
#include <string.h>

#define ECHOMAX         255 /* エコー文字列の最大長 */
#define FILENAME_LEN	100
#define BUF_LEN		    20

/*送信先IPアドレス*/
#define SERV_IP                     "192.168.30.216"
#define INITIAL_BEACON_PORT         (60001)
#define SERV_PORT                   (55000)
#define VIDEO_FILE                  "~/highway_cif.y4m" 
//#define VIDEO_FILE                  "v4l2:///dev/video1"

/* 送信データ量通知メッセージ送信タイミングはsending section毎*/
#define SENDING_SECTION     100
/* #define VIDEO_SENDING_PORT "port " */

#define EXIT_CODE_PORT              (60002)

/* パケット転送に利用する通信媒体 */

enum {
    MESS_WIRED     = 1,
    MESS_WIRELESS  = 0,
    VIDEO_WIRED    = 0,
    VIDEO_WIRELESS = 1,
};
/* 無線ネットワークアドレスは16バイト目以降、50.X, 有線は30.X */
/* e.g. 192.168.30.1 (有線利用) */
/* e.g. 192.168.50.1 (無線利用) */

#define WIRED_NETADDR    (30)
#define WIRELESS_NETADDR (50)

int isSameStr(char *str1, char *str2, int len);
struct in_addr changeIP (struct in_addr ip_addr, int orig_net, int aft_net);


int
main(argc, argv)
    int argc;
    char *argv[];
{

    /* Paramters listed below are used for esstablishing UDP server.      */
    unsigned short     port     = INITIAL_BEACON_PORT; /* port for recv control message */
    int                srcSocket;        /* UDP server (my port)            */
    struct sockaddr_in clientAddr;

    unsigned int        clientAddrLen = sizeof(clientAddr);
    int                 status;
    int                 numrcv;
    char               *buffer;
    u_short             buffer_size;
    /* regulation parameter  */

    u_int            field1; //initial bitrate
   
    //受信ログファイル作成用
    struct timeval current_time;
    int            day,usec;
    
    /*IPアドレス表示部までのパディング*/
    u_short  padding_ip;
    int      True = 1;
    u_int    initialBitrate;       /* 送信はじめビットレート */
    char     videoSendCom[200];
    char     capCom[200];
    struct in_addr clientIP;
    char *device;
    int portNumber;

    /* 終了コード受信待ち用 */
    u_short port_exit = EXIT_CODE_PORT;
    int     exitSocket;
    
     if ( argc != 3) {
        fprintf(stderr, "only enter args %d, You muse specify network interface and port number.\n",argc);
        exit(1);
    }
    
    /* specify network interface capturing packets */
    device = argv[1];
    portNumber = atoi(argv[2]);

    
    /* 受信パケットサイズ */
    buffer_size = sizeof(u_int);
            
    /*バッファ領域確保*/
    if((buffer = (char *)malloc(sizeof(char) * buffer_size)) == NULL) {
        err(EXIT_FAILURE, "fail to allocate memory to the buffer. ReceiveImageDataValueMessage\n");
    }
    
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_port        = htons(port);
    clientAddr.sin_family      = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      
    /* socket */
    srcSocket = socket(AF_INET, SOCK_DGRAM, 0);     /*UDP*/
    setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); /*ソケットの再利用*/

    if ((status = bind(srcSocket, (struct sockaddr *) &clientAddr, sizeof(clientAddr))) < 0) {
        err(EXIT_FAILURE, "fail to bind (ReceiveImageDataValueMessage\n");
    }
   
    clientAddrLen = sizeof(clientAddr);

    printf("waiting fo beacon\n");
    /* 初期ビットレート通知メッセージ受信 */
    numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                      (struct sockaddr *) &clientAddr, &clientAddrLen);

    clientIP = clientAddr.sin_addr;

    /* 送信先アドレス変換 (制御メッセージを有線LANネットで受信した場合、無線IPに置換*/
    if ( VIDEO_WIRELESS ) {
        clientIP = changeIP(clientIP ,WIRED_NETADDR, WIRELESS_NETADDR);
    }
    
    memcpy(&initialBitrate, buffer, buffer_size);

    initialBitrate /= 1000;


    sprintf(capCom,
            "sudo ./notifySendVal %s %d &"
            ,device
            ,portNumber);
    system(capCom);

    
    sprintf(videoSendCom,
            "~/vlc2/vlc-2.0.3/vlc -I \"dummy\" \"$@\" -vvv %s --sout '#transcode{vcodec=mp4v,vb=%d,fps=10,scale=1.0,acodec=none}:udp{dst=%s:%d}' &"
            ,VIDEO_FILE
            ,initialBitrate
            ,inet_ntoa(clientIP)
            ,portNumber);
    system(videoSendCom);

    printf("%s\n", capCom);
    printf("%s\n",videoSendCom);

   
    close(srcSocket);

    /* 終了コード受信待ち状態 */
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_port        = htons(EXIT_CODE_PORT);
    clientAddr.sin_family      = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      
    /* socket */
    exitSocket = socket(AF_INET, SOCK_DGRAM, 0);     /*UDP*/
    setsockopt(exitSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); /*ソケットの再利用*/

    if ((status = bind(exitSocket, (struct sockaddr *) &clientAddr, sizeof(clientAddr))) < 0) {
        err(EXIT_FAILURE, "fail to bind (ReceiveImageDataValueMessage\n");
    }
    
    clientAddrLen = sizeof(clientAddr);

    /* buffer_size == sizeof(int) */
    numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                      (struct sockaddr *) &clientAddr, &clientAddrLen);
    /* プロセスの終了 */
    system("sudo killall vlc notifySendVal");
    exit(0);
    
    
}


/* ２つの文字列比較を行い、str1 eq str2なら0、それ以外は1を返す */
/* 注意 len(str1)<len(str2)は想定していない */
/* str1:比較対象 */
/* str2:探索パターン */
/* len : str2の文字列長 */
/* ２つの文字列比較を行い、str1 eq str2なら0、それ以外は1を返す */
/* 注意 len(str1)<len(str2)は想定していない */
/* str1:比較対象 */
/* str2:探索パターン */
/* len : str2の文字列長 */
int isSameStr(char *str1, char *str2, int len) {

    int i, sameNum = 0;
    int isHit = 0;
    /* printf("call\n"); */

    for(i=0; i<len; i++) {
      

        if (strncmp(str1+i, str2, len)==0) {
            /* printf("str1 %s,  str2 %s len%d\n",str1+i, str2, len); */

            isHit = 1;
            break;
        
        }else {
            sameNum=0;
        }
    }

    /* printf("end\n"); */
    /* 文字列str1, str2がlen分同じ文字列を抱えている:0 else 1*/
    if(isHit) {
        
        return i;
    } else {
        return -1;
    }

    
}
/* IPアドレス置換 */
/* X.X.Y.Xという形式のアドレス(X部分は何が入っていてもよい)を置換基アドレスorig_net==Yとし、 */
/* aft_net==ZとしたときX.X.Z.Xに置換する */
struct in_addr changeIP (struct in_addr ip_addr, int orig_net, int aft_net) {
    char s_orig_ip [20];
    char s_orig_net[5], s_aft_net[5];
    int  s_aft_net_size, s_orig_net_size;
    int  cnt, end;
    int  hit = 0;
    struct in_addr aft_ip;
    int loc = 0;
    
    sprintf(s_orig_ip, "%s",   inet_ntoa(ip_addr));
    sprintf(s_orig_net, ".%d" ,orig_net);
    sprintf(s_aft_net,  ".%d" ,aft_net);

    /* printf("origip %s, net %s aft %s\n", s_orig_ip, s_orig_net, s_aft_net); /\*  *\/ */
    s_orig_net_size = strlen(s_orig_net);
    s_aft_net_size  = strlen(s_aft_net);
    end         = strlen(s_orig_ip) - strlen(s_orig_net);

    for(cnt = 0; cnt < end; cnt++) {
        //printf("call %s\n",s_orig_ip+cnt);
        if((loc = isSameStr(s_orig_ip+cnt, s_orig_net, s_orig_net_size)) >= 0) {
            //printf("s_orig_ip+cnt %s,  s_orig_net %s\n",s_orig_ip+cnt, s_orig_net);
            hit = 1;
            break;
        }
        
    }
    /* 置換 */
    if (hit) {
        strncpy(s_orig_ip+cnt+loc, s_aft_net,  s_aft_net_size);
        /* printf("s_aft ip %s\n" */
        /*        ,s_orig_ip); */
    }
    
    inet_aton(s_orig_ip, &aft_ip);

    return aft_ip;              /* パターン一致がない場合、オリジナルのIPアドレスが返る */

}
