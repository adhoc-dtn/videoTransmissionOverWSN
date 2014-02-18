#!/usr/bin/perl -l
use Sys::Hostname 'hostname';

# videoSenderを繰り返し実行する。
#        使い方 : $ perl recursive_videoSender.pl (実行プログラム) (interface) (port) (初期ビットレート) 
#                  (パターン数）　(１パターンあたりの試行回数)
#        注意　各種パラメータはサーバ側とあわせてくださいね

# 毎回の実験結果で得られたログファイルは別名で保存する

if (scalar(@ARGV)<6 || scalar(@ARGV)>6) {
    die "<Usage>: perl recursive_clientd.pl (executable program) (interface) (port) (startBitrate) (Number of pattern) (Number of try)\n";
}


#実行プログラム名
my $exec_program       = $ARGV[0];
my $interface          = $ARGV[1];
my $port               = $ARGV[2];
my $startBitrate       = $ARGV[3];
my $number_of_pattern  = $ARGV[4];
#試行回数
my $number_of_try      = $ARGV[5];

# 40 kbps(0.5Kbytes)ずつビットレートを加算
my $bitrate_adder      = 40;

# 最大ループ回数
my $loop_max = $number_of_pattern*$number_of_try;

#１回の実験終了時にNICTのNTPサーバにntpdateして時刻合わせする
# 本実験では不可能？
my $ntp_server = "133.243.238.163";


my $try = 0;
my $pattern=0;
#ネットワークエミュレータによるロス設定


system ("sudo ntpdate ${ntp_server};");
$bitrate = $startBitrate;

#試行回数分繰り返し
while ($try < $loop_max) {
    if ($pattern == $number_of_pattern) {
	$pattern = 0;
	$bitrate+=$bitrate_adder;
    }
    printf("(${exec_program}) EXPERIMENT (bitrate ${bitrate}  try ${pattern} port ${port} starts now (alltry ${try}/${loop_max}");
    system ("nohup ./${exec_program} ${interface} ${port};");
    my $name_directory = sprintf("stat${bitrate}kbps_try${pattern}");
    my $make_directory = sprintf("mkdir ${name_directory}");
    system (${make_directory});

    my $move_logfile = sprintf("mv *.log ${name_directory};");
    system (${move_logfile});

    system ("sudo ntpdate ${ntp_server};");
    
    $try++;
    $pattern++;
   
}
printf("[EXPERIMENT SUCCESSFULY EXIT]");


