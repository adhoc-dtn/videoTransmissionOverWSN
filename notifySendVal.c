/*Time-stamp: <Wed Jan 15 10:20:26 JST 2014>*/


/* 送信データ中のipv4のシーケンス番号フィールドを参照し、 */
/* 一定間隔で転送データ量通知メッセージを映像収集ノードに送信する */

/* 入力 : [ネットワークインタフェース名(e.g. eth0, wlan0)]  [宛先ポート番号]*/

/*   */
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
/* (連携プロセスと同期するように修正を加えるべき) */
#define SERV_IP                     "192.168.30.216"
#define INITIAL_BEACON_PORT         (60001)
#define SERV_PORT                   (55000)
#define VIDEO_FILE                  "highway_cif.y4m"

/* 送信データ量通知メッセージ送信タイミングはsending section毎*/
#define SENDING_SECTION     100
/* #define VIDEO_SENDING_PORT "port " */

void DieWithError   (char *errorMessage);
/* 転送データ量通知メッセージ通知 */
void SendValueOfData (u_short start, u_short end, u_int value_of_data );


/* IPヘッダ */
struct sniff_ip {
	u_char ip_vhl;		/* バージョン（上位4ビット）、ヘッダ長（下位4ビット） */
	u_char ip_tos;		/* サービスタイプ */
	u_short ip_len;		/* パケット長 */
	u_short ip_id;		/* 識別子 */
	u_short ip_off;		/* フラグメントオフセット */
#define IP_RF 0x8000		/* 未使用フラグ（必ず0が立つ） */
#define IP_DF 0x4000		/* 分割禁止フラグ */
#define IP_MF 0x2000		/* more fragments フラグ */
#define IP_OFFMASK 0x1fff	/* フラグメントビットマスク */
	u_char ip_ttl;		/* 生存時間（TTL） */
	u_char ip_p;		/* プロトコル */
	u_short ip_sum;		/* チェックサム */
	struct in_addr ip_src,ip_dst; /* 送信元、送信先IPアドレス */
};



int
main(argc, argv)
    int argc;
    char *argv[];
{
    char *device;
    pcap_t *pd;
    int snaplen = 64;
    int pflag = 0;
    int timeout = 1000;
    char ebuf[PCAP_ERRBUF_SIZE];
    char portNumber[30];
    bpf_u_int32 localnet, netmask;
    pcap_handler callback;
    void print_ethaddr(u_char *, const struct pcap_pkthdr *, const u_char *);
    struct bpf_program fcode;

    if ( argc != 3) {
        fprintf(stderr, "only enter args %d, You muse specify network interface and port number.\n",argc);
        exit(1);
    }
    
    /* specify network interface capturing packets */
    device = argv[1];
    sprintf(portNumber, "port %s", argv[2]);

    /* printf("waitingBeacon\n"); */
    /* /\* ビーコンを受信するまでブロッキングする *\/ */
    /* waitingInitialBitrateMess(INITIAL_BEACON_PORT, atoi(argv[2])); */

    
    /* open network interface with on-line mode */
    if ((pd = pcap_open_live(device, snaplen, !pflag, timeout, ebuf)) == NULL) {
        fprintf(stderr, "Can't open pcap deivce\n");
        exit(1);
    }
    
    /* get informations of network interface */
    if (pcap_lookupnet(device, &localnet, &netmask, ebuf) < 0) {
        fprintf(stderr, "Can't get interface informartions\n");
        exit(1);
    }
    
    /* setting and compiling packet filter */
#if 1
    /* In this example, capture HTTP and FTP packets only */
    /*if (pcap_compile(pd, &fcode, "port 80 or 20 or 21", 1, netmask) < 0) {*/
    
    if (pcap_compile(pd, &fcode, portNumber, 1, netmask) < 0) {
        fprintf(stderr, "can't compile fileter\n");
        exit(1);
    }
    if (pcap_setfilter(pd, &fcode) < 0) {
        fprintf(stderr, "can't set filter\n");
        exit(1);
    }
#endif
    
    /* set call back function for output */
    /* in this case output is print-out procedure for ethernet addresses */
    callback = print_ethaddr;
    
    if (pcap_loop(pd, -1, callback, NULL) < 0) {
        (void)fprintf(stderr, "pcap_loop: error occurred\n");
        exit(1);
    }
    
    /* close capture device */
    pcap_close(pd);

    exit(0);
}
#define SIZE_ETHERNET 14
/* print time stamp and ethernet addresses from passed data by pcap_loop */
void
print_ethaddr(userdata, h, p)
    u_char *userdata;              /* in this sample, userdata is NULL */
    const struct pcap_pkthdr *h;   /* structure h containing time stamp */
                                   /* and packet size, and etc ... */
    const u_char *p;               /* body of packet data */
{
    int i;
    //    struct ether_header *eh;
    struct sniff_ip    *ih;
    struct timeval    current_time;
    int               day,usec;
    double            d_current_time;

    static int sequence_last = -1; /*前回までに送信した送信シーケンス番号*/
    int        sequence_number;
    int         diff;
    static long int value_all = 0;
    FILE *fp;
    char *filename = "logfile_sendPacket.log";
    
   
    //eh = (struct ether_header *)p;
    ih    = (struct sniff_ip *)(p + SIZE_ETHERNET);

    sequence_number = (int)htons(ih->ip_id);
    /*printf("%hu ",  htons(ih->ip_id) );
        
    printf("%s", inet_ntoa(ih->ip_src));
    printf(" -> ");
    
    printf("%s", inet_ntoa(ih->ip_dst));

    printf(" len = %d\n", h->len);*/

    value_all += h->len;
    if (sequence_last == -1 ){
        sequence_last = sequence_number;
    } else  {
        if (sequence_last > sequence_number) {
            diff = sequence_number - sequence_last + USHRT_MAX;
        } else {
            diff = sequence_number - sequence_last;
        }
        
        if (diff == SENDING_SECTION -1 ) {
            /* 送信データ量通知メッセージ送信 */
	  //            printf("Sending message...\n");
            SendValueOfData (sequence_last, sequence_number, value_all );
            sequence_last = sequence_number+1;
            value_all = 0;
                
        }
    }
    printf("%d.%06d send volume %d ipaddr %s  seq %d\n"
            ,day
            ,usec
            ,h->len
            ,inet_ntoa(ih->ip_dst)
            ,htons(ih->ip_id));
    
    /* ---受信データ量のログを残す(この部分は重複あり)------ */
    if((fp=fopen(filename,"a")) == NULL) {
        printf("FILE OPEN ERROR %s\n",filename);
        exit (EXIT_FAILURE);
    }
    // timevalを初期化
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    fprintf(fp,
            "%d.%06d send volume %d ipaddr %s  seq %d\n"
            ,day
            ,usec
            ,h->len
            ,inet_ntoa(ih->ip_dst)
            ,htons(ih->ip_id));
    fclose(fp);
    
   
}

void DieWithError(char *errorMessage){ /* 外部エラー処理関数 */
  perror(errorMessage);
  exit(1);
}



void SendValueOfData (u_short start, ushort end, unsigned value_of_data ) {
    
    int sock;                         /* ソケットディスクリプタ */
    struct sockaddr_in servAddr;  /* サーバのアドレス */
    struct sockaddr_in fromAddr;      /* 送信元のアドレス */
    unsigned short servPort;      /* サーバのポート番号 */
    unsigned int fromSize;            /* recvfrom()のアドレスの入出力サイズ */
    char *servIP;                     /* サーバのIPアドレス */
    char *string;                 /* サーバへ送信する文字列 */
    char echoBuffer[ECHOMAX+1];       /* エコー文字列の受信用バッファ */
    int stringLen;                /* 送信文字列の長さ */
    int respStringLen;                 /* 受信した応答の長さ */
    char buf[8];                      /* u_short 2 byte + 2 byte, unsigned 4 byte + null*/
    char filesize[BUF_LEN]="";
    long long int message = 0;
    unsigned short int one = 1;
    u_short field1=0;
    u_short field2=0;
    unsigned int field3 = 0;
    int          byteSize;
    /* ログ記録用 */
    FILE *fp;
    char *filename = "logfile_notifyValMess.log";
    u_char low_delay;

    servIP   = SERV_IP; 
    servPort = SERV_PORT; /* 指定のポート番号があれば使用 */

    field1       = start;
    field2       = end;
    field3       = value_of_data;
    
    /* UDPデータグラムソケットの作成 */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");
    
    /* サーバのアドレス構造体の作成 */
    memset(&servAddr, 0, sizeof(servAddr));   /* 構造体にゼロを埋める */
    servAddr.sin_family = AF_INET;                /* インターネットアドレスファミリ */
    servAddr.sin_addr.s_addr = inet_addr(servIP); /* サーバのIPアドレス */
    servAddr.sin_port = htons(servPort);      /* サーバのポート番号 */
    memset(buf, 0, 9);   /* 構造体にゼロを埋める */

   
    memcpy(buf, &field1, sizeof(u_short));
    memcpy(buf+2*sizeof(char), &field2, sizeof(u_short));
    memcpy(buf+4*sizeof(char), &field3, sizeof(int));

    
    printf("%hu, %hu, %u \n", field1, field2, field3);

    low_delay = 46 << 2;
    setsockopt (sock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
    byteSize = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
        
    close(sock);
    /* ---受信データ量のログを残す(この部分は重複あり)------ */
    if((fp=fopen(filename,"a")) == NULL) {
        printf("FILE OPEN ERROR %s\n",filename);
        exit (EXIT_FAILURE);
    }

    fprintf(fp,
            "from %d end %d volume %d\n"
            ,field1
            ,field2
            ,field3);
    fclose(fp);

}
