# makefile 
# (lpcapのオプションでエラーする場合、libpcap-devをインストールしてください)

# ----------------
# make時の注意
# 基本的に、Cファイルを更新したら
# 	$ make
# するだけでよい。更新したCファイルのみ、オブジェクトファイルが生成される

# ただし、ファイル「common.h」のパラメータを変えた場合、
# 	$ make clean; make
# とすること

#コンパイラ、コンパイルオプション
CC	        = gcc
#デバッグするためには最適化オプションしないuほうがよい
# 終わったら -O0オプション消すこと(-O0は最適化せずにコード入れ替えしないオプション)
CFLAGS	        = -lpthread -lrt -lm -lpcap  -O0  -g
#CFLAGS	        = -g  -lpthread -lrt -lm

#オブジェクトファイルの組み合わせ
CLIENT_OBJGROUP   = notifySendVal.o 
CEXE_OBJGROUP     = videoSender.o
SERVER_OBJGROUP   = videoRateController.o 


#実行ファイル名
EXECUTABLE_CLIENT = notifySendVal
EXECUTABLE_EXE    = videoSender
EXECUTABLE_SERVER = videoRateController

#クライアント、サーバ両方の実行ファイルを作成する
all:notifySendVal videoRateController videoSender
#all: clientImageSender

#クライアントプログラム(映像送信ノード用)
notifySendVal:${CLIENT_OBJGROUP}
	$(CC) ${CLIENT_OBJGROUP} -o ${EXECUTABLE_CLIENT} ${CFLAGS}
videoSender:${CEXE_OBJGROUP}
	$(CC) ${CEXE_OBJGROUP} -o ${EXECUTABLE_EXE} ${CFLAGS}
#サーバプログラム(映像収集ノード用　制御側)
videoRateController:${SERVER_OBJGROUP}
	$(CC) ${SERVER_OBJGROUP} -o ${EXECUTABLE_SERVER} ${CFLAGS}

clean:
	\rm *.o *~ ${EXECUTABLE_SERVER} ${EXECUTABLE_CLIENT} ${EXECUTABLE_EXE}
