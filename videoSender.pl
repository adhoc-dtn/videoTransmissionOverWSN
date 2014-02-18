#!/usr/bin/perl


# このプログラムを実行する際に、sudoでパスワードが求められない状態となるように
# あらかじめvisudoで設定を変えてください


use Socket;     # Socket モジュールを使う
#use strict;
use threads; 
use threads::shared; 

$beacon_port  = "60001";
$dest_ip      = "192.168.30.216";
$file_name    = "~/highway_cif.y4m";
#送信側データ量のメッセージ受信部分

my $usage_str   = "<Usage>: perl ./videoSender.pl (interface) (dest_port number).\n";

if (scalar(@ARGV) == 2) {
    #printf("You set option %s IP as %s\n",$ARGV[0], $ARGV[1])
    $interface = $ARGV[0];    
    $dest_port = $ARGV[1];
    
}else {
    die $usage_str;
}
printf("[waitingBeacon]\n");
socket(SOCKET, PF_INET, SOCK_DGRAM, 0) or die "fail to create scoket in videoSender.pl\n";
bind(SOCKET, pack_sockaddr_in($beacon_port, INADDR_ANY));
recv(SOCKET, $buf, 10, 0);

$message      = substr($buf,0,4);
$init_bitrate = unpack("h8", $message);
# ビーコン受信、vlcスタート、ならびに送信パケット監視

printf("beacon received (initial bitrate == %d bps\n",$init_bitrate);


# キャプチャ開始
my $capt_com    = sprintf("sudo ./notifySendVal %s %d"
			  ,$interface
			  ,$dest_port);

if (system($capt_com) < 0) {
    die "fail to exec ${capt_com}\n";
}
# vlcコマンドに与えるビットレート単位はkbpsとなる
my $init_bitrate /=1000;
# 映像送信開始

my $vlc_command  = sprintf("~/vlc2/vlc-2.0.3/vlc -I \"dummy\" \"$@\" -vvv %s --sout '#transcode{vcodec=mp4v,vb=%d,fps=10,scale=1,acodec=none}:udp{dst=%s:%d}'"
			  ,$file_name
			  ,$init_bitrate
			  ,$dest_ip
			  ,$dest_port);


if (system($vlc_command) < 0) {
    die "fail to exec ${vlc_command}\n";
}

printf("${vlc_command}\n");
