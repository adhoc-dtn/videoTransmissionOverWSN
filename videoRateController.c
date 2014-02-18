/*Time-stamp: <Wed Jan 15 11:11:31 JST 2014>*/


/* 送信データ中のipv4のシーケンス番号フィールドを参照し、 */
/* 一定間隔で転送データ量通知メッセージを映像収集ノードに送信する */

/* 経路表の例 無線には50.Xを利用し、有線では30.Xを利用する*/
/* ただし、libpcapが無線IF利用時に誤作動(MACの再送パケットがそのまま出力されている？)
 * する性質があるので、送信端末を2端末利用、２段階構成する*/

/* (1) 映像送信ノード（映像エンコード、配信する（配信時、すべてのパケットは有線IFで出ていく) */
/* (2) 無線IFノード (映像配信を無線で行うときに動作する。ルータ。SNATで送信元IPを50.Xに置換
 *     DNATで宛先端末50.X（映像送信ノードor受信ノード）に対して30.Xに置換処理する */

/* 映像受信ノードの経路表の例 */
/* maselab@mse216:~/src$ route -n */
/* カーネルIP経路テーブル */
/* 受信先サイト    ゲートウェイ    ネットマスク   フラグ Metric Ref 使用数 インタフェース */
/* 0.0.0.0         192.168.30.254  0.0.0.0         UG    0      0        0 eth0 */
/* 169.254.0.0     0.0.0.0         255.255.0.0     U     1000   0        0 eth0 */
/* 192.168.30.0    0.0.0.0         255.255.255.0   U     1      0        0 eth0 */
/* 192.168.50.212  192.168.30.236  255.255.255.255 UGH   0      0        0 eth0 */
/* 192.168.50.213  192.168.30.236  255.255.255.255 UGH   0      0        0 eth0 */
/* 192.168.50.214  192.168.30.236  255.255.255.255 UGH   0      0        0 eth0 */
/* maselab@mse216:~/src$                                                        */

/* SNAT,DNATの設定方法 */
/* PREROUTINGは経路表を見てルーチングする前段階 POSTROUTINGは経路表を見た後で適用される */
/* 原則、DNATはPREROUTING、SNATはPOSTROUTINGで設定しなければならない*/
/* -d: 宛先IPアドレスに該当したら--to-destinationに変換 */
/* -s: 送信元IPアドレスに該当したら--toに変換 */

/* iptables -t nat -A PREROUTING  -d 192.168.50.216 -j DNAT --to-destination 192.168.30.216; */
/* iptables -t nat -A POSTROUTING -s 192.168.30.216 -j SNAT --to 192.168.50.216 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <pcap.h>
#include <signal.h>

#include <arpa/inet.h>
#include <time.h>

#include <unistd.h> /* for close()に必要 */
#include <fcntl.h>   // O_RDONLY等
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

/* 初期ビットレート 単位 bits/sec (int) */
/* 注意、vlc実行コマンドに与える レートはkbps単位になる*/
/* 実際にレート制御を行う場合は、bits/secとなる */
#define INITIAL_BITRATE         (1500000)
/* 輻輳検知しきい値 */
#define GAMMA1                  (0.9)
/* 輻輳回復しきい値 */
#define GAMMA2                  (0.95)


/* ビットレート削減パラメータ(輻輳回避) */
#define ALPHA                   (0.1)
/* ビットレート増加パラメータ(輻輳回復) */
#define BETA                    (0.1)
/* 受信成功率算出タイミング 単位：秒(浮動小数点OK) */
#define TIMER_CALC_RECV_RATIO   (10)

/* 実験サイクル数 */
#define NUM_CYCLE_EXP           (20)


#define ECHOMAX         255 /* エコー文字列の最大長 */
#define FILENAME_LEN	100
#define BUF_LEN		    20

/*メッセージ送信先IPアドレス*/
#define SERV_IP "192.168.30.216"
#define AMOUNT_SEND_MESS_PORT       (55000)
#define VLC_CONTROL_MESS_PORT       (60000)
#define INITIAL_BEACON_PORT         (60001)
#define EXIT_CODE_PORT              (60002)
#define VIDEO_SENDING_PORT "port 50002 or 50003 or 50004"


/* シーケンス番号２つ、合計転送サイズ */
#define NOTIFY_DATA_VOLUME_SIZE     (2*sizeof(u_short)+sizeof(u_int))


/* 監視対象ポート */
/* クライアントリストをgetするスクリプト */
#define GET_CLIENT          "./get_destination.pl"
#define ALL_DEST            "all_dest.dat"
#define ALL_DEST_WIRELESS   "all_dest_wireless.dat"
#define ALL_DEST_WIRED      "all_dest_wired.dat"

/* クライアントとの通信を行うデバイス */
#define COM_DEVICE   "eth0"

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

void DieWithError              (char *errorMessage);
void SendValueOfData           (u_short start, ushort end, unsigned value_of_data );
/* 指定ポートにビットレート制御メッセージを送信する */
void sendingBitrateControlMess (u_int bitrate, struct in_addr ipSrc, u_short destPort);
/* 受信成功率を算出する */
void CalcDataRecvRate          (int signum);
/* 転送データ量通知メッセージの受信 */
void *ReceiveImageDataValueMessage(void *param);
/* 終了コード送信処理 */
void sendExitCode();
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
	u_char ip_ttl;	    	/* 生存時間（TTL） */
	u_char ip_p;	    	/* プロトコル */
	u_short ip_sum;	    	/* チェックサム */
	struct in_addr ip_src,ip_dst; /* 送信元、送信先IPアドレス */
};

/* 転送データ量通知メッセージを格納するリスト */
struct transferDataMess {
    struct in_addr ipSrc;       /* 送信元IPアドレス */
    u_short        start;       /* シーケンス送信はじめ */
    u_short        end;         /* シーケンス送信おわり */
    u_int          dataSize;      /* 上区間送信データ量 */
    struct transferDataMess *next;
};
/* ヘッド */
struct transferDataMess tDM_Head;
/* 上記リスト操作時のmutex */
pthread_mutex_t tDM_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 受信データ格納用リスト (１パケットを表す)*/
struct recvDataInfo {
    u_short              seq;         /* ipv4シーケンス番号 */
    int                  dataSize;    /* データサイズ*/
    struct recvDataInfo *next;        /* nextポインタ */

};

/* 送信元IPアドレスリスト */
struct ipSrcList {
    struct in_addr      ipSrc;
    struct recvDataInfo rDI_Head;  /* 上記リストのヘッド */
    pthread_mutex_t     rDI_mutex; /* 上記リスト操作時のmutex */
    struct ipSrcList    *next;     /* next */
};

struct ipSrcList iSL_Head;      /* 上記リストのヘッド */
pthread_mutex_t  iSL_mutex = PTHREAD_MUTEX_INITIALIZER; /* 上記リストのmutex */

struct clientList {
    struct in_addr     ipSrc;
    struct clientList *next;
} cL_Head;


int isSameStr(char *str1, char *str2, int len);
struct in_addr changeIP (struct in_addr ip_addr, int orig_net, int aft_net);

/* timer  */
struct sigaction  act, oldact;
timer_t           tid;
struct itimerspec itval;
/* 現在設定されているビットレート */
int               BitrateCurrent;

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
    bpf_u_int32 localnet, netmask;
    pcap_handler callback;
    void print_ethaddr(u_char *, const struct pcap_pkthdr *, const u_char *);
    struct bpf_program fcode;

    /* すべての子ノードのIPアドレスに対して、ユニキャストする */
    char getDestCommand[100];
    struct clientList *cL_new, *cL_cur;
    char               one_addr[30];
    short              one_line_max     = 20;
    FILE              *route_fp;
    /* sending interval*/
    double interval_int, interval_frac;

    pthread_t threadID;
    /* データ送信端末格納ファイル */
    char      *dest_file;
    char      replace_command[100];
    
    /* 以下、前処理 */
    /* 引数チェック */
    if (argc != 2) {
        fprintf(stderr, "You muse specify network interface.\n");
        exit(EXIT_FAILURE);
    }

    /* 転送データ量通知メッセージ受信用スレッドの起動 */
    if (pthread_create(&threadID
                       ,NULL
                       ,ReceiveImageDataValueMessage
                       ,NULL) != 0) {
        printf("fail to create receive image data value message\n");
        exit(EXIT_FAILURE);
    }
    /* すべての宛先ノードに動画送信処理開始命令を出す */
    /* 宛先ノードをリスト化し、ビットレート制御メッセージ送信時にも使用する */

    sprintf(getDestCommand, "%s %s > %s"
            ,GET_CLIENT
            ,COM_DEVICE
            ,ALL_DEST);
    system(getDestCommand);

    if(MESS_WIRED) {
        sprintf(replace_command, "sed -e 's/%d/%d/g' %s > %s"
                ,WIRELESS_NETADDR, WIRED_NETADDR, ALL_DEST, ALL_DEST_WIRED );
        system (replace_command);
        dest_file = ALL_DEST_WIRED; /* 以後のread対象のファイル名を変更 */
        
        
    } else {
        dest_file = ALL_DEST;
    }
    

    if((route_fp = fopen(dest_file, "r")) == NULL) {
        err(EXIT_FAILURE, "fail to open route file sendPacketToNext route_fp\n");
    }
    cL_Head.next = NULL;
    /* 新規挿入時のみ、cL_curはヘッドから走査 移行では、nextから走査するので注意 */
    cL_cur       = &cL_Head;

    while((fgets(one_addr, one_line_max-1, route_fp )) != NULL) {
        one_addr[strlen(one_addr)-1] = '\0'; /*改行コード->\0に*/

        cL_new       = (struct clientList *)malloc( sizeof (struct clientList) );
        cL_new->next = NULL;
        if (inet_aton( one_addr, &cL_new->ipSrc) == 0) { /* 文字列からバイナリへ変換 */
            err(EXIT_FAILURE, "inet_aton detect wrong address\n");
        }
        cL_cur->next  = cL_new; //追加
        cL_cur        = cL_cur->next;
        /* 初期ビットレート送信 */
        BitrateCurrent = INITIAL_BITRATE;
        printf("sending beacon to %s (initial bitrate %d bps)\n",inet_ntoa(cL_new->ipSrc),BitrateCurrent);
        sendingBitrateControlMess(BitrateCurrent, cL_new->ipSrc, INITIAL_BEACON_PORT);

    }
    fclose(route_fp);
    /* ビーコン送信終わり */

    
    /*受信成功率算出関数(CalcDataRecvRate)のコールバック登録  */
    memset(&act,    0, sizeof(struct sigaction)); 
    memset(&oldact, 0, sizeof(struct sigaction)); 
    
    act.sa_handler = CalcDataRecvRate; 
    act.sa_flags   = SA_RESTART; 
    if(sigaction(SIGALRM, &act, &oldact) < 0) { 
        err (EXIT_FAILURE, "sigaction()");
      
    } 
    /* タイマを設定 */
    interval_frac = modf(TIMER_CALC_RECV_RATIO, &interval_int);
    
    itval.it_value.tv_sec      = interval_int;  
    itval.it_value.tv_nsec     = interval_frac*1000000000; 
    itval.it_interval.tv_sec   = interval_int;
    itval.it_interval.tv_nsec  = interval_frac*1000000000;

    if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) { 
        err (EXIT_FAILURE, "timer_create is fail");
    } 
    
    if(timer_settime(tid, 0, &itval, NULL) < 0) { 
        err (EXIT_FAILURE, "timer_settting is fail");
    } 
    
    /* パケットキャプチャ開始 */
    /* キャプチャデバイスの設定(libpcapの) */
    device = argv[1];
    
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
    
    if (pcap_compile(pd, &fcode, VIDEO_SENDING_PORT, 1, netmask) < 0) {
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

    static long  int sequence_last = 0; /*前回までに送信した送信シーケンス番号*/
    long int         sequence_number;
    long int         diff;
    static long int value_all = 0;

    /* ソースipアドレスリスト */
    struct ipSrcList     *iSL_new, *iSL_cur, *iSL_prev, *iSL_proc;
    /* 受信パケットリスト */
    struct recvDataInfo  *rDI_new, *rDI_cur, *rDI_prev, *rDI_proc;

    struct in_addr ipSrc;

    /* ログ記録用 */
    FILE *fp;
    char *filename = "logfile_recvPacket.log";
    
    
    //eh = (struct ether_header *)p;
    ih    = (struct sniff_ip *)(p + SIZE_ETHERNET);

    /* 送信元IPアドレス  リスト内になければ挿入*/
    ipSrc = ih->ip_src;
    pthread_mutex_lock(&iSL_mutex); /* lock */
    iSL_prev = &iSL_Head;
    for (iSL_cur = iSL_Head.next; iSL_cur != NULL; ) {
        if (memcmp(&iSL_cur->ipSrc, &ipSrc, sizeof(struct in_addr)) == 0) {
            break; //アドレスがヒット
        }
        iSL_prev = iSL_cur;
        iSL_cur = iSL_cur->next;
    }

    if(iSL_cur == NULL) {       /* 上記の探索結果、ヒットしない ->アドレスリスト新規作成*/
        iSL_new                 = (struct ipSrcList * )malloc(sizeof(struct ipSrcList));
        iSL_new->ipSrc          = ipSrc;
        iSL_new->rDI_Head.next  = NULL;
        
        iSL_new->rDI_mutex      = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
        iSL_proc                = iSL_new; //今後の操作対象ポインタ
        iSL_prev->next = iSL_new;
        iSL_new->next= NULL;
        
    } else {/* 上記の探索結果、ヒット*/
        iSL_proc = iSL_cur; //操作対象ポインタ
    }
    pthread_mutex_unlock(&iSL_mutex); /* unlock */

    /* 対応するIPアドレスリスト以下、受信パケットリストへの追加 */
    pthread_mutex_lock(&iSL_proc->rDI_mutex);/* lock */
    rDI_cur  = iSL_proc->rDI_Head.next;
    rDI_prev = &iSL_proc->rDI_Head;

    rDI_new      = (struct recvDataInfo *)malloc(sizeof(struct recvDataInfo));
    /* シーケンス番号 */
    rDI_new->seq      = htons(ih->ip_id);
    rDI_new->dataSize = h->len;
    /* リストに挿入 */
    rDI_new->next     = rDI_cur;
    rDI_prev->next    = rDI_new;
    pthread_mutex_unlock(&iSL_proc->rDI_mutex); /* unlock */

    /* 受信パケットの書き込み */
    // 受信時刻
    gettimeofday(&current_time, NULL);
    day          = (int)current_time.tv_sec;
    usec         = (int)current_time.tv_usec;
    printf("%d.%06d received volume %d ipaddr %s  seq %d\n"
            ,day
            ,usec
            ,h->len
            ,inet_ntoa(ipSrc)
            ,htons(ih->ip_id));
    
    /* ---受信データ量のログを残す(この部分は重複あり)------ */
    if((fp=fopen(filename,"a")) == NULL) {
        printf("FILE OPEN ERROR %s\n",filename);
        exit (EXIT_FAILURE);
    }

    fprintf(fp,
            "%d.%06d received volume %d ipaddr %s  seq %d\n"
            ,day
            ,usec
            ,h->len
            ,inet_ntoa(ipSrc)
            ,htons(ih->ip_id));
    fclose(fp);
    
    
}

void DieWithError(char *errorMessage){ /* 外部エラー処理関数 */
  perror(errorMessage);
  exit(1);
}

/*ビットレート制御メッセージ送信  */
void sendingBitrateControlMess(u_int bitrate, struct in_addr ipSrc, u_short destPort) {

    int                sock;      /* ソケットディスクリプタ */
    struct sockaddr_in servAddr;  /* サーバのアドレス */
    unsigned short     servPort;  /* サーバのポート番号 */
    char               echoBuffer[ECHOMAX+1];  /* エコー文字列の受信用バッファ */
    
    char               buf[sizeof(int)];               /* u_short 2 byte + 2 byte, unsigned 4 byte + null*/
    int                byteSize;
    double interval_int, interval_frac;
    u_char low_delay;

    
    /* UDPデータグラムソケットの作成 */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");
    low_delay = 46 << 2;
    setsockopt (sock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
    /* サーバのアドレス構造体の作成 */
    memset(&servAddr, 0, sizeof(servAddr));   /* 構造体にゼロを埋める */
    servAddr.sin_family      = AF_INET;                /* インターネットアドレスファミリ */
    servAddr.sin_addr        = ipSrc; /* サーバのIPアドレス */
    servAddr.sin_port        = htons(destPort);      /* サーバのポート番号 */
    
    memcpy(buf, &bitrate, sizeof(u_int));

    printf("[sendingBitrateMess] bitrate,%d to %s\n", bitrate, inet_ntoa(ipSrc)); 
    
    byteSize = sendto(sock, buf, sizeof(u_int), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
        
    close(sock);
    
    /* // 現在のタイマの解除 */
    /* timer_delete(tid); */

    /* interval_frac = modf(TIMER_CALC_RECV_RATIO, &interval_int); */
    /* //printf("interval_frac = %lf, interval_int = %lf\n",interval_frac,interval_int); */
    /* // 新しいタイマ設定 */
    /* itval.it_value.tv_sec      = interval_int;   */
    /* itval.it_value.tv_nsec     = interval_frac*1000000000;  */
    /* itval.it_interval.tv_sec   = interval_int; */
    /* itval.it_interval.tv_nsec  = interval_frac*1000000000;  */
}

/* 転送データ量通知メッセージ受信関数 */

/*転送データ量通知メッセージ受信部分*/
void *ReceiveImageDataValueMessage(void *param)
{
    
    /* Paramters listed below are used for esstablishing UDP server.      */
    unsigned short     port     = AMOUNT_SEND_MESS_PORT; /* port for recv control message */
    int                srcSocket;        /* UDP server (my port)            */
    struct sockaddr_in clientAddr;

    unsigned int        clientAddrLen = sizeof(clientAddr);
    int                 status;
    int                 numrcv;
    char               *buffer;
    u_short             buffer_size;
    /* regulation parameter  */

    u_short            field1, field2; //sequence number [start through  end].
    u_int              field3;         //amount of sent data size from a node.
    
    //現在の末尾のリストのポインタ；
    struct transferDataMess  *mess_cur, *mess_prev, *mess_new;
  
    //受信ログファイル作成用
    FILE *fp_sendval;
    char *logfilename = "logfile_recvValNotifyMess.log";
    struct timeval current_time;
    int            day,usec;
    
    /*IPアドレス表示部までのパディング*/
    u_short  padding_ip;
    int True = 1;

    struct in_addr clientIP;
    
    buffer_size = NOTIFY_DATA_VOLUME_SIZE;
            
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


    
    while(1) {
        //転送データ量通知メッセージ受信
        numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                          (struct sockaddr *) &clientAddr, &clientAddrLen);

        clientIP = clientAddr.sin_addr;
        /* 制御メッセージの配送が有線、映像が無線の場合、無線IPに統一する */

        if(MESS_WIRED && VIDEO_WIRELESS) {
            clientIP = changeIP(clientIP ,WIRED_NETADDR, WIRELESS_NETADDR);

        }
        memcpy( &field1,   buffer,                     sizeof(u_short) );
        memcpy( &field2,   buffer + sizeof(u_short),   sizeof(u_short) );
        memcpy( &field3,   buffer + 2*sizeof(u_short), sizeof(u_int)   );
        printf("[ReceiveImageDataValueMessage %d bytes]  Data val mess is received. ip %s, from %d to %d amnt %d bytes\n"
               ,numrcv
               ,inet_ntoa(clientIP)
               ,field1
               ,field2
               ,field3
        );            
        /*転送データ量通知メッセージ・リスト挿入準備*/
        mess_new       = (struct transferDataMess  *)malloc(sizeof(struct transferDataMess));
        mess_new->ipSrc    = clientIP;
        mess_new->start    = field1;
        mess_new->end      = field2;
        mess_new->dataSize = field3;
        /*ロックして挿入*/
        pthread_mutex_lock   (&tDM_mutex);
        mess_new->next  = tDM_Head.next;
        tDM_Head.next   = mess_new;
        pthread_mutex_unlock (&tDM_mutex);
        /*mutexアンロック*/

        //ログファイル作成
        if((fp_sendval=fopen(logfilename,"a")) == NULL) {
            printf("FILE OPEN ERROR %s\n",logfilename);
            exit (EXIT_FAILURE);
        }
        gettimeofday(&current_time, NULL);
        day   = (int)current_time.tv_sec;
        usec  = (int)current_time.tv_usec;

        fprintf(fp_sendval
                ,"%d.%06d seq_start %u seq_end %d total %u from %s\n"
                ,day
                ,usec
                ,field1
                ,field2
                ,field3
                ,inet_ntoa(clientIP)
        );
        fclose(fp_sendval);
    }
    //ここにはこない
    close(srcSocket);
}
    

/* 受信成功率算出 (SIGALRMによってタイマ時間経過後にコールするコールバック関数とする)*/
void CalcDataRecvRate         (int signum) {
    static u_int  current_cycle     = 0; 
    u_int         amount_of_recv    = 0;
    u_int         amount_of_send    = 0;
    double        recv_ratio         = 0.0;

    /*パケットのポインタ*/
    /* ソースIPリスト */
    struct ipSrcList          *iSL_cur,    *iSL_prev,   *iSL_proc;
    /* 受信パケットデータ */
    struct recvDataInfo       *rDI_cur,    *rDI_prev,   *rDI_proc;
    /*転送データ量通知メッセージのポインタ*/
    struct transferDataMess    *mess_cur, *mess_prev;

    struct clientList  *cL_cur, *cL_new;

    int     diff = 0;
    u_short last_seq;
    
    u_int   list_size = 0;
    
    struct in_addr ip_mess, ip_pac;
    u_short sequence_from, sequence_end, rDI_sequence;

      
    int overflow   =0;
    int del        =0;             /*項目削除時1に*/

    /* 受信成功率算出時の時刻 */
    struct timeval current_time;
    int            day,usec;    
    /*受信率のログをとる*/
    char *recvratio_logfile = "logfile_calcRecvRatio.log";
    char *actmess_logfile = "logfile_act_mess.log";

    FILE *fp;
    FILE *fp_actmess;
    int  sequence = 0;
    
    /*周期短縮メッセージ送信時1 else 0*/
    u_int    message_interval_makes_short;
    /*周期延長メッセージ送信時1 else 0*/
    u_int    message_interval_makes_long;

    
    amount_of_send = amount_of_recv = 0; //送受信データ量
    message_interval_makes_long  = 0; //可変周期設定方式におけるデバッグ用パラメータ
    message_interval_makes_short = 0;

   
    printf("[CalcDataRecvRate] called\n"); 

    /*転送データ量通知メッセージリスト・ロック*/
    pthread_mutex_lock(&tDM_mutex);
    mess_prev = &tDM_Head;
    mess_cur  = tDM_Head.next;
    pthread_mutex_unlock(&tDM_mutex);

    /* ---受信データ量のログを残す(この部分は重複あり)------ */
    if((fp_actmess=fopen(actmess_logfile,"a")) == NULL) {
        printf("FILE OPEN ERROR %s\n",actmess_logfile);
        exit (EXIT_FAILURE);
    }
    while (mess_cur != NULL) {
        /*送信データ量通知メッセージ1つ抽出*/
        ip_mess         = mess_cur->ipSrc;
        sequence_from   = mess_cur->start;
        sequence_end    = mess_cur->end;
        amount_of_send += mess_cur->dataSize;
        //転送データ量通知メッセージの内容をを表示
        printf(  "[  message  ] ipaddr:%s , sec1:%5hu , sec2:%5hu datasize: %10u amount %u\n"
                 ,inet_ntoa(ip_mess) 
                 ,sequence_from
                 ,sequence_end
                 ,mess_cur->dataSize
                 ,amount_of_send);
    

        fprintf(fp_actmess,
                "[  message  ] ipaddr:%s , sec1:%5hu , sec2:%5hu datasize: %10u amount %u\n"
                 ,inet_ntoa(ip_mess) 
                 ,sequence_from
                 ,sequence_end
                 ,mess_cur->dataSize
                ,amount_of_send);
        
        
        /* 対応する送信元IPアドレスリストを探す */
        pthread_mutex_lock(&iSL_mutex);
        iSL_cur  = iSL_Head.next;
        iSL_prev = &iSL_Head;
        pthread_mutex_unlock(&iSL_mutex);

        while(iSL_cur != NULL){
            if (memcmp(&ip_mess, &iSL_cur->ipSrc, sizeof(struct in_addr)) == 0) {

                /* printf(  "[  in iSL ] ipaddr:%s\n " */
                /*          ,inet_ntoa(ip_mess) ); */
	
                break; //発見
            }
            pthread_mutex_lock(&iSL_mutex);
            iSL_cur = iSL_cur->next;
            pthread_mutex_unlock(&iSL_mutex);

        }

        //ヒットした場合、受信データ量を加算する(ここでヒットしないことはない
       
        if(iSL_cur != NULL ) {
            
            pthread_mutex_lock(&iSL_cur->rDI_mutex);
            rDI_cur      =  iSL_cur->rDI_Head.next;
            rDI_prev     = &iSL_cur->rDI_Head;
            pthread_mutex_unlock(&iSL_cur->rDI_mutex);

            /* シーケンス始まりと終わりの間に、USHORTの最大値を超えて0に戻った場合 */
            if(sequence_from > sequence_end ) {
                overflow = 1;
            } else {
                overflow = 0;
            }
           
            while(rDI_cur != NULL ) {
                del = 0;
                if ( overflow==1 ) { /*overflowによってシーケンス番号の範囲を変更*/
                    if ( rDI_cur->seq >= sequence_from || rDI_cur->seq <= sequence_end) {
                        //転送データ量通知メッセージの内容をを表示
                        printf(  "[  actual ] ipaddr:%s, sec %d, datasize: %d\n "
                                 ,inet_ntoa( iSL_cur->ipSrc) 
                                 ,rDI_cur->seq
                                 ,rDI_cur->dataSize);
                       

                        

            
                        amount_of_recv += rDI_cur->dataSize;
                        fprintf(fp_actmess,
                                "[  actual ] ipaddr:%s , sec:%5hu , datasize: %10u amount %u\n"
                                ,inet_ntoa( iSL_cur->ipSrc) 
                                ,rDI_cur->seq
                                ,rDI_cur->dataSize
                                ,amount_of_recv);
                        del = 1; /*項目削除フラグ*/
                    }
                    
                } else if ( overflow == 0 ) {
                    if ( rDI_cur->seq >= sequence_from && rDI_cur->seq <= sequence_end) {
                        //転送データ量通知メッセージの内容をを表示
                        printf(  "[  actual ] ipaddr:%s, sec %d, datasize: %d\n  "
                                 ,inet_ntoa( iSL_cur->ipSrc) 
                                 ,rDI_cur->seq
                                 ,rDI_cur->dataSize);
                     
                        
                        
            
                        amount_of_recv += rDI_cur->dataSize;
                         fprintf(fp_actmess,
                                "[  actual ] ipaddr:%s , sec:%5hu , datasize: %10u amount %u\n"
                                ,inet_ntoa( iSL_cur->ipSrc) 
                                ,rDI_cur->seq
                                ,rDI_cur->dataSize
                                ,amount_of_recv);
                      
                        del = 1;
                    }
                }
                pthread_mutex_lock(&iSL_cur->rDI_mutex);

                /* 項目削除＆次へのポインタへ */
                /* if (del == 1) { */
                /*     rDI_cur        = rDI_cur->next; */
                /*     free(rDI_prev->next); */
                /*     rDI_prev->next = rDI_cur; */
                /* } else { */
                    rDI_prev = rDI_cur;
                                        
                    rDI_cur  = rDI_cur->next;
                /* } */
                pthread_mutex_unlock(&iSL_cur->rDI_mutex);
            }
            
         
        } //if iSL_cur != NULL

    
        //参照した転送データ量通知メッセージの領域解放
        pthread_mutex_lock   (&tDM_mutex);
        mess_cur = mess_cur->next;
        free(mess_prev->next);
        mess_prev->next = mess_cur;
        pthread_mutex_unlock (&tDM_mutex);
        
                
    } //end of 転送データ量通知メッセージ探索、受信成功率算出用データ準備

     
    
    /*画像データ受信成功率算出*/
    if (amount_of_send > 0) { //この[条件式]=[転送データ量通知メッセージが来ている] とする
        //recv_rate  = (double)amount_of_recv/amount_of_send;
        printf("[Recv rate calculation]-----------------------------------------\n");

        recv_ratio = (double)amount_of_recv/amount_of_send;
      
        /* ビットレート制御メッセージを送信 */
        if ( recv_ratio < GAMMA1) {
            BitrateCurrent = (1-ALPHA)*BitrateCurrent;

            /* 各クライアントノードにユニキャスト */
            cL_cur       = cL_Head.next;
            while (cL_cur != NULL ) {
                sendingBitrateControlMess(BitrateCurrent, cL_cur->ipSrc, VLC_CONTROL_MESS_PORT);
                
                cL_cur       =  cL_cur->next;
            }
            message_interval_makes_long  = 1;
            message_interval_makes_short = 0;
	    
        } else if ( recv_ratio > GAMMA2) {
            BitrateCurrent = (1+ BETA)*BitrateCurrent;
            /* 各クライアントノードにユニキャスト */
            cL_cur       = cL_Head.next;;
            while (cL_cur != NULL ) {
                sendingBitrateControlMess(BitrateCurrent, cL_cur->ipSrc, VLC_CONTROL_MESS_PORT);

                cL_cur       =  cL_cur->next;
            }
            message_interval_makes_long  = 0;
            message_interval_makes_short = 1;

        }
        
    }else {
        //        printf("...(any data is not received)\n");
        //デフォルト値2.0なので0にする
        recv_ratio = 0.0;
    
    }
   
    /*画像データ受信成功率のログ書き込み*/
    if((fp = fopen(recvratio_logfile, "a")) == NULL)  {
        err (EXIT_FAILURE, "cannot open :%s(for saving receiving ratio)\n", recvratio_logfile);
    }
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;

    /* printf("[CalcDataRecvRate] st 6 before making log\n"); */
            
    printf("[RECV RATIO %.5lf (in message %d, amount of recv %d)\n"
           ,recv_ratio,amount_of_send,amount_of_recv);
    fprintf(fp,
            "%d.%06d,recv_ratio,%lf,in message,%d,amount of recv,%d,set_bitrated,%d,congestion,%u,congestion_resolution,%u\n",
            day,
            usec,
            recv_ratio,
            amount_of_send,
            amount_of_recv,
            BitrateCurrent,
            message_interval_makes_long,
            message_interval_makes_short);
    fclose(fp);
    fprintf(fp_actmess,
            "%d.%06d,recv_ratio,%lf,in message,%d,amount of recv,%d,set_bitrated,%d,congestion,%u,congestion_resolution,%u\n",
            day,
            usec,
            recv_ratio,
            amount_of_send,
            amount_of_recv,
            BitrateCurrent,
            message_interval_makes_long,
            message_interval_makes_short);
    fclose(fp_actmess);


    current_cycle++;
    if(current_cycle == NUM_CYCLE_EXP) {
        /* 各クライアントノードにユニキャスト */
        cL_cur       = cL_Head.next;
        while (cL_cur != NULL ) {
            sendExitCode(cL_cur->ipSrc); /* 実験終了コードを送信 */
            cL_cur   =  cL_cur->next;
        }
        
        printf("%d cycle. sending exit code\n",current_cycle);
        sleep(10);
        exit(0);
    }
}
/* 終了コード送信処理 */
void sendExitCode(struct in_addr ipDst) {
    int                sock;      /* ソケットディスクリプタ */
    struct sockaddr_in servAddr;  /* サーバのアドレス */
    unsigned short     servPort;  /* サーバのポート番号 */
    char               echoBuffer[ECHOMAX+1];  /* エコー文字列の受信用バッファ */
    
    char               buf[sizeof(int)];               /* u_short 2 byte + 2 byte, unsigned 4 byte + null*/
    int                mess = 1;
    int                byteSize;
    double             interval_int, interval_frac;
    u_char             low_delay;
    u_short            destPort = EXIT_CODE_PORT;
    
    /* UDPデータグラムソケットの作成 */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");
    
    low_delay = 46 << 2;
    setsockopt (sock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));

    /* サーバのアドレス構造体の作成 */
    memset(&servAddr, 0, sizeof(servAddr));       /* 構造体にゼロを埋める */
    servAddr.sin_family      = AF_INET;           /* インターネットアドレスファミリ */
    servAddr.sin_addr        = ipDst;             /* サーバのIPアドレス */
    servAddr.sin_port        = htons(destPort);   /* サーバのポート番号 */
    
    memcpy(buf, &mess, sizeof(u_int));

    printf("sending exit code mess to %s\n", inet_ntoa(ipDst)); 
    
    byteSize = sendto(sock, buf, sizeof(u_int), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
        
    close(sock);


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
