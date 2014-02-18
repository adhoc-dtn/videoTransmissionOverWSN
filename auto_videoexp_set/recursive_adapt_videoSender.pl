#!/usr/bin/perl -l
use Sys::Hostname 'hostname';

# videoSenderを繰り返し実行する。
#        使い方 : $ perl recursive_videoSender.pl (実行プログラム) (interface) (port) (初期ビットレート) 
#                  (パターン数）　(１パターンあたりの試行回数)
#        注意　各種パラメータはサーバ側とあわせてくださいね

# 毎回の実験結果で得られたログファイルは別名で保存する

if (scalar(@ARGV)<5 || scalar(@ARGV)>5) {
    die "<Usage>: perl recursive_clientd.pl (executable program) (interface) (port) (startBitrate) (Number of try)\n";
}


#実行プログラム名
my $exec_program       = $ARGV[0];
my $interface          = $ARGV[1];
my $port               = $ARGV[2];
my $startBitrate       = $ARGV[3];
#試行回数
my $number_of_try      = $ARGV[4];

# 最大ループ回数
my $loop_max = $number_of_try;

#１回の実験終了時にNICTのNTPサーバにntpdateして時刻合わせする
# 本実験では不可能？
my $ntp_server = "133.243.238.163";


my $try = 0;

system ("sudo ntpdate ${ntp_server};");
$bitrate = $startBitrate;

#試行回数分繰り返し
while ($try < $loop_max) {

    printf("(${exec_program}) EXPERIMENT (bitrate ${bitrate}  port ${port} starts now (alltry ${try}/${loop_max}");
    system ("nohup ./${exec_program} ${interface} ${port};");
    my $name_directory = sprintf("stat${bitrate}kbps_try${try}");
    my $make_directory = sprintf("mkdir ${name_directory}");
    system (${make_directory});

    my $move_logfile = sprintf("mv *.log ${name_directory};");
    system (${move_logfile});

    system ("sudo ntpdate ${ntp_server};");
    
    $try++;
   
}
printf("[EXPERIMENT SUCCESSFULY EXIT]");


