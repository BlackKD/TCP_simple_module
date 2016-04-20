#include "lib/netprogram.h"
#include <sys/time.h>
#include <time.h>
#include "stcp_client.h"

#define TABLE_LEN 20
static client_tcb_t *ctcb_table[TABLE_LEN];
static pthread_mutex_t tcb_mutex[TABLE_LEN];
int connfd;

client_tcb_t *create_ctcb(unsigned client_port) {
	client_tcb_t *p = (client_tcb_t *)malloc(sizeof(client_tcb_t));
	if( p != NULL ) {
		p->client_portNum = client_port;
		p->state = CLOSED;
		p->next_seqNum = 1;
		p->sendBufHead = NULL;
		p->sendBufunSent = NULL;
		p->sendBufTail = NULL;
		p->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
		p->unAck_segNum = 0;
		pthread_mutex_init(p->bufMutex, NULL);
	}
	else 
		printf("create_ctcb error! malloc error.\n");

	return p;
}
 
 /*
  * Find thd tcb whose client_portNum is des_port;
  * On success, the tcb's index is returned;
  * On failure, -1 is returned.
  */
int search_ctcb(unsigned dest_port) {
	int i;
	for(i = 0; i < TABLE_LEN; i ++) {
		if(ctcb_table[i] != NULL && ctcb_table[i]->client_portNum == dest_port)
			return i;
	} 
	return -1;
}

/*
 * Send segs, and change state
 * On error, 0 is returned
 * On success, 1 is returned
 */
int sendseg(int conn, client_tcb_t *p, seg_t *segPtr) {
	// check whether the connection is okay
	struct sockaddr_in sa;
	int len = sizeof(sa);
	if((getpeername(conn, (struct sockaddr *)&sa, &len) < 0) && errno == ENOTCONN) {
		printf("The other side is closed.\n");
		return 0;
	}

	// send the segment
	if( !sip_sendseg(conn, segPtr) )
		return 0;
	
	int seg_type = segPtr->header.type;
	
	// change state
	switch(p->state) {
	 	case CLOSED: {
			if(seg_type == SYN)
				p->state = SYNSENT;
			else 
				printf("Trying to send not-a-SYN seg during CLOSED.\n");
			break;
		}	
	 	case CONNECTED: {
			if(seg_type == FIN)
				p->state = FINWAIT;
			else if(seg_type != DATA)
				printf("Trying to send a seg besides FIN or DATA.\n");
			break;
		}
		case SYNSENT: {
			if(seg_type != SYN)
				printf("Trying to send a Not-SYN while in SYNSENT.\n");
			break;
		}
		case FINWAIT: {
			if(seg_type != FIN)
				printf("Trying to send a Not-FIN while in FINWAIT.\n");
			break;
		}
		default: 
			printf("Trying to send a seg while in Unknown state.\n");
	} 

	return 1;
}

/*
 * If the ack is not received in  time_out*max_times microseconds, then return 1
 * else return 0
 */
int wait_syn_ack(int conn, int sock, seg_t *segPtr, int time_out, int max_times) { 
	printf("wait_syn_ack\n");

	int seconds = 0;
	int times = 0;
	
	client_tcb_t *tcb_p = ctcb_table[sock];
	
	do {
		pthread_mutex_lock(&(tcb_mutex[sock]));
		if (tcb_p->state == CONNECTED) { 
			pthread_mutex_unlock(&(tcb_mutex[sock]));
			break;
		}
		pthread_mutex_unlock(&(tcb_mutex[sock])); 

		// check time
		if(seconds > time_out) { 
			// fail
			if(times >= max_times ) {
				printf("give up transmiting SYN\n");
				return 0;
			}
			// retransmit 
			printf("retransmit SYN times: %d, dest_port: %d\n", times, segPtr->header.dest_port);
			if( !sendseg(conn, tcb_p, segPtr) ) return 0;
			seconds = 0;
			times ++;
		}
		// make this thread sleep 1 microsecond
		seconds ++;
		usleep(1);
	} while(1);

	return 1;
}

/*
 * If the ack is received in time_out microseconds*max_times, then return 1
 * else return -1
 */
int wait_fin_ack(int conn, int sock, seg_t *segPtr, int time_out, int max_times) {	
	printf("wait_fin_ack\n");

	int seconds = 0;
	int times = 0;
	client_tcb_t *tcb_p = ctcb_table[sock];
	
	do {
		pthread_mutex_lock(&(tcb_mutex[sock]));
		if (tcb_p->state == CLOSED) { 
			pthread_mutex_unlock(&(tcb_mutex[sock]));
			break;
		}
		pthread_mutex_unlock(&(tcb_mutex[sock]));

		// check time
		if(seconds > time_out) { 
			// fail
			if(times >= max_times ) {
				printf("give up transmiting FIN\n");
				return 0;
			}
			// retransmit 
			printf("retransmit FIN times: %d, dest_port: %d\n", times, segPtr->header.dest_port);
			if( !sendseg(conn, tcb_p, segPtr) ) return 0;
			seconds = 0;
			times ++;
		}
		// make this thread sleep 1 microsecond
		seconds ++;
		usleep(1);
	} while(1); 

	return 1;
}

void handle_synack(seg_t *p) {
	printf("handle_synack\n");

	stcp_hdr_t *h = &(p->header);
	int sock = search_ctcb(h->dest_port);
	if(sock < 0) return;

	client_tcb_t *tcb = ctcb_table[sock];
	
	pthread_mutex_lock(&(tcb_mutex[sock]));
	if(tcb->state == SYNSENT && h->ack_num == tcb->next_seqNum) {
		printf("port %d state to CONNECTED\n", h->dest_port);
		tcb->state = CONNECTED;
	}
	pthread_mutex_unlock(&(tcb_mutex[sock]));
}

void handle_finack(seg_t *p) {
	printf("handle_finack\n");

	stcp_hdr_t *h = &(p->header);
	int sock = search_ctcb(h->dest_port);
	if(sock < 0) return;

	client_tcb_t *tcb = ctcb_table[sock];
	
	pthread_mutex_lock(&(tcb_mutex[sock]));
	if(tcb->state == FINWAIT && h->ack_num == tcb->next_seqNum) {
		printf("port %d state to CLOSED\n", h->dest_port);
		tcb->state = CLOSED;
	}
	pthread_mutex_unlock(&(tcb_mutex[sock]));
}

int sendUnsent(client_tcb_t *tcb) {
	printf("send the unsent segs\n");
	if(tcb == NULL) return 0;

	segBuf_t *unsentHead = tcb->sendBufunSent;
	while(unsentHead != NULL) {
		if( !sendData(tcb, unsentHead) ) 
			return 0;
		else {
			unsentHead = unsentHead->next;
			tcb->sendBufunSent = unsentHead;
		}
	}
	return 0;
}

void handle_dataack(seg_t *p) {
	printf("handle_dataack\n");

	stcp_hdr_t *h = &(p->header);
	int sock = search_ctcb(h->dest_port);
	if(sock < 0) return;

	client_tcb_t *tcb = ctcb_table[sock];

	pthread_mutex_lock(tcb->bufMutex);
	
	// ack the unacked
	segBuf_t *sb = tcb->sendBufHead;
	while(sb != NULL) {
		segBuf_t *sb_temp = sb;
		sb = sb->next;
		if((sb_temp->seg).header.seq_num <= p->header.ack_num) {
			tcb->sendBufHead = sb;
			free(sb_temp);
			tcb->unAck_segNum --;
		}
	}
	// send the unsent
	sendUnsent(tcb);

	pthread_mutex_unlock(tcb->bufMutex);
}

segBuf_t *create_sbuf() {
	segBuf_t *p = (segBuf_t *)malloc(sizeof(segBuf_t));
	if(p != NULL) {
		p->sentTime = 0;
		p->next = NULL;
		memset(&(p->seg), 0, sizeof(seg_t));
	} 
	else 
		printf("create_sbuf error.\n");

	return p;
}

/*
 * Add a segBuf to "sent but not acked" buffer head;
 * The case that unAck_segNum >= GBN_WINDOW should be handled before
 * On success, 1 will be returned.
 * On error, 0 will be returned.
 */
int add2notackedBuf(client_tcb_t *tcb, segBuf_t *sb) {
	if(tcb == NULL || sb == NULL) return 0;

	segBuf_t *head = tcb->sendBufHead;
	if(head == NULL) {
		tcb->sendBufHead = sb;
		sb->next = NULL;
	}
	else {
		sb->next = head;
		tcb->sendBufHead = sb;
	}

	return 1;
}

/* 
 * If the length of "sent but not acked" buffer is not less than GBN_WINDOW, then
 * add the seg to the "unsent" buffer tail;
 * else, send it
 * On success, 1 will be returned
 * On error, 0 will be returned
 */
int sendData(client_tcb_t *tcb, segBuf_t *sb) {
	printf("sendData\n");
	if(tcb == NULL || sb == NULL) return 0;

	if(tcb->unAck_segNum < GBN_WINDOW) { // send it
		if( !sendseg(connfd, tcb, &(sb->seg)) ) {
			printf("Send a DATA failed.\n");
			return 0;
		}
		sb->sentTime = 1;
		return add2notackedBuf(tcb, sb);
	}
	else { // add it to the "unsent" buffer
		printf("notAcked buffer is full and add the segBuf to unsent buffer\n");
		segBuf_t *unsentHead = tcb->sendBufunSent;
		segBuf_t *unsentTail = tcb->sendBufTail;

		sb->next = NULL;
		sb->sentTime = 0;
		if(unsentHead == NULL && unsentTail == NULL) // empty
			tcb->sendBufunSent = tcb->sendBufTail = sb;
		else {
			tcb->sendBufTail = sb;
			unsentTail->next = sb;
		}
	}
	return 1;
}

/*
 * retransmit all the unAcked from tail
 */
void retransmitData(client_tcb_t *tcb, segBuf_t *cur) {
	// excepction
	if(tcb == NULL) return;

	// end the recursion
	if(cur == NULL) return;

	// first enter retransmitData
	// the retransmitted segs will add to the unAcked buffer's head
	if( cur == tcb->sendBufHead ) tcb->sendBufHead = NULL;
    
	// recursion
	retransmitData(tcb, cur->next);

	// deal with this node
	tcb->unAck_segNum --;
	// send it, and
	// the case that unAcked buffer is full will not appear here,
	// so the tcb->sendBufHead will not be changed by sendDat
	sendData(tcb, cur);
}

void *checkDataTimeout(void *arg) {
	int i;
	while(1) {
		for(i = 0; i < TABLE_LEN; i ++) {
			client_tcb_t *tcb = ctcb_table[i];
			if(tcb != NULL) {
				pthread_mutex_lock(tcb->bufMutex);
			
				segBuf_t *sb = tcb->sendBufHead;
				sb->sentTime += SENDBUF_POLLING_INTERVAL;
				// if newest unAcked seg timeout, retransmit all the unacked
				// else add all the unscked segs' sentTime
				if(sb->sentTime >= DATA_TIMEOUT)
					retransmitData(tcb, tcb->sendBufHead);
				else 
					while( (sb = sb->next) != NULL )
						sb->sentTime += SENDBUF_POLLING_INTERVAL;

				pthread_mutex_unlock(tcb->bufMutex);
			}
		}
		usleep(SENDBUF_POLLING_INTERVAL);
	}
	return NULL;

}

void free_sendBuf(client_tcb_t *tcb) {
	segBuf_t *p;
	
	free(tcb->bufMutex);
	tcb->bufMutex = NULL;

	// sent but not acked
	p = tcb->sendBufHead;
	while(p != NULL) {
		p = p->next;
		free(p);
	}
	tcb->sendBufHead = NULL;

	// unsent
	p = tcb->sendBufunSent;
	while(p != NULL) {
		p = p->next;
		free(p);
	}
	tcb->sendBufunSent = NULL;
	tcb->sendBufTail = NULL;
}
/*����Ӧ�ò�Ľӿ�*/

// stcp�ͻ��˳�ʼ��
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// ��������ص�����TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void stcp_client_init(int conn) {
	pthread_t tid;
	pthread_t dto;
	int i;
	for(i = 0; i < TABLE_LEN; i ++) {
		ctcb_table[i] = NULL;
		pthread_mutex_init(&(tcb_mutex[i]), NULL);
	}

	connfd = conn;

    pthread_create(&tid, NULL, &seghandler, NULL);
	pthread_create(&dto, NULL, &checkDataTimeout, NULL);

    return;
}


// ����һ���ͻ���TCB��Ŀ, �����׽���������
//
// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_sock(unsigned int client_port) {
	printf("stcp_client_sock.\n");

	int i;
	for(i = 0; i < TABLE_LEN; i ++) {
		if( ctcb_table[i] == NULL ) {
			client_tcb_t *p;
			if( (p = create_ctcb(client_port)) != NULL ) {
				ctcb_table[i] = p;
				break;
			}
			else {
				i = -1;
				break;
			}
		}
	}

    return i;
}

// ����STCP������
//
// ��������������ӷ�����. �����׽���ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_connect(int sockfd, unsigned int server_port) {
	printf("stcp_client_connect\n");

	client_tcb_t *p = ctcb_table[sockfd];
	seg_t seg;
	int seq_num;
	if( p == NULL ) {
		printf("connect error! ctcb_table[%d] is NULL.\n", sockfd);
		return -1;
	}
	
	p->server_portNum = server_port;
	seq_num = p->next_seqNum ++;

	memset(&seg, 0, sizeof(seg_t));
	set_stcp_hdr( &(seg.header), p->client_portNum, server_port, seq_num, 0, 0, SYN, 0, 0 );

	// send SYN 
	if( !sendseg(connfd, p, &seg) )
		return -1;

	// wait SYNACK
	if( wait_syn_ack(connfd, sockfd, &seg, SYN_TIMEOUT, SYN_MAX_RETRY) ) {
		p->state = CONNECTED;
		return 1;
	}
	else {
		p->state = CLOSED;
		return -1;
	}
}

// �������ݸ�STCP������. �������ʹ���׽���ID�ҵ�TCB���е���Ŀ. 
// Ȼ����ʹ���ṩ�����ݴ���segBuf, �������ӵ����ͻ�����������. 
// ������ͻ������ڲ�������֮ǰΪ��, һ����Ϊsendbuf_timer���߳̾ͻ�����. 
// ÿ��SENDBUF_ROLLING_INTERVALʱ���ѯ���ͻ������Լ���Ƿ��г�ʱ�¼�����.
// ��������ڳɹ�ʱ����1�����򷵻�-1. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_send(int sockfd, void* data, unsigned int length)
{
	segBuf_t *sb = create_sbuf();
	if(sb == NULL) 
		return -1;

	client_tcb_t *p = ctcb_table[sockfd];
	if(p == NULL)
		return -1;

	seg_t *seg = &(sb->seg);
	unsigned seq_num = p->next_seqNum;
	p->next_seqNum += length;
	// set header
	set_stcp_hdr( &(seg->header), p->client_portNum, p->server_portNum, seq_num, 0, sizeof(stcp_hdr_t)+length, DATA, 0, 0 );
	// copy data
	int i;
	char *data_begin = (char *)(seg->data);
	for(i = 0; i < length; i ++) {
		data_begin[i] = ((char *)data)[i];
	}

	// unsentBuffer will handle it
	pthread_mutex_lock(ctcb_table[sockfd]->bufMutex);

	int ret = sendData(ctcb_table[sockfd], sb);
	
	pthread_mutex_unlock(ctcb_table[sockfd]->bufMutex);
	
	return (!ret) ? -1 : 0;
}

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_disconnect(int sockfd) {
	printf("stcp_client_disconnect\n");

	client_tcb_t *p = ctcb_table[sockfd];
	seg_t seg;
	int seq_num;
	if(p == NULL) {
		printf("In stcp_client_disconnect: ctcb_table[%d] is NULL.\n", sockfd);
		return -1;
	}

	seq_num = p->next_seqNum ++;
	memset(&seg, 0, sizeof(seg_t));
	set_stcp_hdr(&(seg.header), p->client_portNum, p->server_portNum, seq_num, 0, 0, FIN, 0, 0);

	// send FIN
	if( !sendseg(connfd, p, &seg) )
		return -1;
	
	// creat a timer and wait FIN ack
	if( wait_fin_ack(connfd, sockfd, &seg, FIN_TIMEOUT, FIN_MAX_RETRY) ) {
		p->state = CLOSED;
		return 1;
	}
  
  	return -1;
}

// �ر�STCP�ͻ�
//
// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd) {
	printf("stcp_client_close\n");
	client_tcb_t *p = ctcb_table[sockfd];
	if(p == NULL) {
		printf("In stcp_client_close: ctcb_table[%d] is NULL.\n", sockfd);
		return -1;
	}
	else {
		free_sendBuf(p);
		free(p);
		p = NULL;
		return 1;
	}
}

// �������ε��߳�
//
// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg) {
	int lost;
	seg_t *seg_p = (seg_t *)malloc(sizeof(seg_t));
	while(1) {
		memset(seg_p, 0, sizeof(seg_t));
		lost = sip_recvseg(connfd, seg_p);

		if( !lost ){
			switch(seg_p->header.type) {
				case SYNACK:  handle_synack(seg_p); break;
				case FINACK:  handle_finack(seg_p); break;
				case DATAACK: handle_dataack(seg_p); break;
				case DATA: 
				case SYN:
				case FIN:
				default:      printf("Un-handled segment type.\n");
			}
		}
		else { // lost

		}
	}

	free(seg_p);
    return 0;
}
