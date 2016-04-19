//�ļ���: server/app_stress_server.c

//����: ����ѹ�����԰汾�ķ������������. ����������ͨ���ڿͻ��˺ͷ�����֮�䴴��TCP����,�����ص������. 
//Ȼ��������stcp_server_init()��ʼ��STCP������. ��ͨ������stcp_server_sock()��stcp_server_accept()����һ���׽��ֲ��ȴ����Կͻ��˵�����.
//��Ȼ������ļ�����. ����֮��, ������һ��������, �����ļ����ݲ��������浽receivedtext.txt�ļ���. 
//���, ������ͨ������stcp_server_close()�ر��׽���. �ص������ͨ������son_stop()ֹͣ.

//��������: 2015��

//����: ��

//���: STCP������״̬

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "../common/constants.h"
#include "stcp_server.h"

//����һ������, ʹ�ÿͻ��˶˿ں�87�ͷ������˿ں�88. 
#define CLIENTPORT1 87
#define SERVERPORT1 88
//�ڽ��յ��ļ����ݱ������, �������ȴ�10��, Ȼ��ر�����.
#define WAITTIME 10

#define MAXLINE 4096 //�����������˿ں�
#define SERV_PORT 3000 //�������δ��������������Ŀ
#define LISTENQ 8

//�������ͨ���ڿͻ��ͷ�����֮�䴴��TCP�����������ص������. ������TCP�׽���������, STCP��ʹ�ø����������Ͷ�. ���TCP����ʧ��, ����-1.
int son_start() {

  //����Ҫ��д����Ĵ���.

      socklen_t clilen;
    int listenfd;
    //char buf[MAXLINE];
    struct sockaddr_in cliaddr,servaddr;
    listenfd = socket(AF_INET,SOCK_STREAM,0);
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    servaddr.sin_family=AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(SON_PORT);
    bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
    listen(listenfd,LISTENQ);//��ʼ����
    clilen = sizeof(cliaddr);
    connfd = accept(listenfd,(struct sockaddr *)&cliaddr,&clilen);
    return connfd;
}

//�������ͨ���رտͻ��ͷ�����֮���TCP������ֹͣ�ص������
void son_stop(int son_conn) {

  //����Ҫ��д����Ĵ���.
    printf("close son_conn\n");
        close(son_conn);
}

int main() {
	//���ڶ����ʵ����������
	srand(time(NULL));

	//�����ص�����㲢��ȡ�ص������TCP�׽���������
	int son_conn = son_start();
	if(son_conn<0) {
		printf("can not start overlay network\n");
	}

	//��ʼ��STCP������
	stcp_server_init(son_conn);

	//�ڶ˿�SERVERPORT1�ϴ���STCP�������׽��� 
	int sockfd= stcp_server_sock(SERVERPORT1);
	if(sockfd<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//��������������STCP�ͻ��˵����� 
	stcp_server_accept(sockfd);

	//���Ƚ����ļ�����, Ȼ������ļ����� 
	int fileLen;
	stcp_server_recv(sockfd,&fileLen,sizeof(int));
	char* buf = (char*) malloc(fileLen);
	stcp_server_recv(sockfd,buf,fileLen);

	//�����յ����ļ����ݱ��浽�ļ�receivedtext.txt��
	FILE* f;
	f = fopen("receivedtext.txt","a");
	fwrite(buf,fileLen,1,f);
	fclose(f);
	free(buf);

	sleep(WAITTIME);

	//�ر�STCP������ 
	if(stcp_server_close(sockfd)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}				

	//ֹͣ�ص������
	son_stop(son_conn);
}
