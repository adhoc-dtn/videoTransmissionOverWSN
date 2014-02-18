/*
 * [get_sending_datasize.c]
 *
 *   dump packets on ethernet interface specified by user.
 *   set first argument to interface name when invoke this command.
 */

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

/*宛先ポート番号*/
/**#define PORT (1234)**/

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
    char port[20];
    
    pcap_t *pd;
    int snaplen = 64;
    int pflag = 0;
    int timeout = 1;
    char ebuf[PCAP_ERRBUF_SIZE];
    bpf_u_int32 localnet, netmask;
    pcap_handler callback;
    void print_ethaddr(u_char *, const struct pcap_pkthdr *, const u_char *);
    struct bpf_program fcode;
    
    
    //printf("PRE\n");
    if (argc != 2) {
        printf("You muse specify network interface.\n");
        printf("packetdump interface port\n");
        exit(1);
    }

    /* specify network interface capturing packets */
    device = argv[1];
    sprintf(port,"port %s",argv[2]);
    /* open network interface with on-line mode */
    //printf("OPEN\n");
    if ((pd = pcap_open_live(device, snaplen, !pflag, timeout, ebuf)) == NULL) {
        printf("Can't open pcap deivce\n");
        exit(1);
    }

    /* get informations of network interface */
    if (pcap_lookupnet(device, &localnet, &netmask, ebuf) < 0) {
        printf("Can't get interface informartions\n");
        exit(1);
    }

    /* setting and compiling packet filter */
#if 1
    /* In this example, capture HTTP and FTP packets only */
    /*if (pcap_compile(pd, &fcode, "port 80 or 20 or 21", 1, netmask) < 0) {*/
    
    if (pcap_compile(pd, &fcode, port , 1, netmask) < 0) {
        printf("can't compile fileter\n");
        exit(1);
    }
    if (pcap_setfilter(pd, &fcode) < 0) {
        printf("can't set filter\n");
        exit(1);
    }
#endif

    /* set call back function for output */
    /* in this case output is print-out procedure for ethernet addresses */
    callback = print_ethaddr;

    /* loop packet capture util picking 1024 packets up from interface. */
    /* after 1024 packets dumped, pcap_loop function will finish. */
    /* argument #4 NULL means we have no data to pass call back function. */
    //printf("LOOP\n");

    if (pcap_loop(pd, -1, callback, NULL) < 0) {
        printf("pcap_loop: error occurred\n");
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
    struct timeval current_time;
    int         day,usec;
    double      d_current_time;
    
    /* システム時刻関係 */
    /*time_t target_time;
    struct tm* target_time_tm;
    char* time_string;
    int string_length=20;
   
    
    time_string=(char *)malloc(string_length*sizeof(char));
    
    (void)time(&target_time);
    target_time_tm = localtime(&target_time);
    (void)strftime(time_string,string_length,"%Y %m/%d %H:%M:%S",target_time_tm);
    
    fprintf(stdout,"%s,",time_string);*/
    

    // timevalを初期化
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    printf("%d.%06d ",day,usec);
    //eh = (struct ether_header *)p;
    ih    = (struct sniff_ip *)(p + SIZE_ETHERNET);
    //    printf(":");
    /* print source ethernet address */
    //    for (i = 0; i < 6; ++i) {
    //printf("%02x:", (int)eh->ether_shost[i]);
    //}
    printf("%s", inet_ntoa(ih->ip_src));
    printf(" -> ");

    /* print destination ethernet address */
    //    for (i = 0; i < 6; ++i) {
    //	printf("%02x:", (int)eh->ether_dhost[i]);
    //}
    printf("%s", inet_ntoa(ih->ip_dst));
    /* print ethernet frame(packet) size */
    printf(" len = %d\n", h->len-42);
}
