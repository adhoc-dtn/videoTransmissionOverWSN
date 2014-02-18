#!/usr/bin/perl -l

# regulation periodにおける試行回数を指定し、CISPを実行する。
#        使い方 : $ perl recursive_videoRateController.pl (実行プログラム) 
#                    (通信デバイス名)  (interface)  (初期ビットレート) (パターン数）　(１パターンあたりの試行回数)

# 毎回の実験結果で得られたログファイルは別名保存する

if (scalar(@ARGV)<4 || scalar(@ARGV)>4) {
    die "<Usage>: perl recursive_clientd.pl (executable program) (interface) (startBitrate) (Number of try)\n";
}


#実行プログラム名
my $exec_program       = $ARGV[0];
my $interface          = $ARGV[1];
my $startBitrate       = $ARGV[2];
#試行回数
my $number_of_try      = $ARGV[3];


# 最大ループ回数
my $loop_max = $number_of_try;

# nict ntp server
my $ntp_server = "133.243.238.163";
#クライアントプログラムの実行後に実行
system ("sudo ntpdate ${ntp_server};");
sleep 10;

$bitrate = $startBitrate;
my $try = 0;
my $pattern=0;
#試行回数分繰り返し
while ($try < $loop_max) {
    
    printf("(${exec_program})  (bitrate ${bitrate}  starts now alltry ${try}/${loop_max}");
    
    system ("nohup ./${exec_program} ${interface} ${bitrate};");

    my $name_directory = sprintf("adapt${bitrate}kbps_try${try}");
    my $make_directory = sprintf("mkdir ${name_directory}");
    system (${make_directory});

    my $move_logfile = sprintf("mv *.log ${name_directory};");
    system (${move_logfile});

    system ("sudo ntpdate ${ntp_server};");
    
    $try++;
   
    sleep 10; 
}

