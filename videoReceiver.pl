#!/usr/bin/perl


# このプログラムを実行する際に、sudoでパスワードが求められない状態となるように
# あらかじめvisudoで設定を変えてください

if (scalar(@ARGV) == 1) {
    #printf("You set option %s IP as %s\n",$ARGV[0], $ARGV[1])
    $interface = $ARGV[0];    

    
}else {
    die $usage_str;
}

# レートコントローラ起動
my $rate_ctrl    = sprintf("sudo ./videoRateController %s"
			   ,$interface );

if (system($rate_ctrl) < 0) {
    die "fail to exec ${rate_ctrl}\n";
}
