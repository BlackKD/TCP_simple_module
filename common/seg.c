//
// 文件名: seg.c

// 描述: 这个文件包含用于发送和接收STCP段的接口sip_sendseg() and sip_rcvseg(), 及其支持函数的实现. 
//
// 创建日期: 2015年
//

#include "seg.h"
#include "stdio.h"

//
//
//  用于客户端和服务器的SIP API 
//  =======================================
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指c导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: sip_sendseg()和sip_recvseg()是由网络层提供的服务, 即SIP提供给STCP.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// 通过重叠网络(在本实验中，是一个TCP连接)发送STCP段. 因为TCP以字节流形式发送数据, 
// 为了通过重叠网络TCP连接发送STCP段, 你需要在传输STCP段时，在它的开头和结尾加上分隔符. 
// 即首先发送表明一个段开始的特殊字符"!&"; 然后发送seg_t; 最后发送表明一个段结束的特殊字符"!#".  
// 成功时返回1, 失败时返回-1. sip_sendseg()首先使用send()发送两个字符, 然后使用send()发送seg_t,
// 最后使用send()发送表明段结束的两个字符.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

int sip_sendseg(int connection, seg_t* segPtr)
{
	
   segPtr->header.checksum = checksum(segPtr);
   printf("header.checksum %d\n",segPtr->header.checksum);
       char *c = (char *)(&(segPtr->header.checksum));
   /*if(c[0] == '`') {
	   printf("checksum0: %c %d\n", c[0], c[0]);
	   exit(0);
   }
   if(c[1] == '`') {
	   printf("checksum1: %c %d\n", c[1], c[1]);
	   exit(0);
   }*/
   char buffer[1504];
    memset(&buffer,0,sizeof(buffer));
    buffer[0] = '`';
    buffer[1] = '&';

	int j;
	for(j = 0; j < sizeof(seg_t); j ++)
		buffer[2+j] = ((char*)(segPtr))[j];
    buffer[2+j] = '`';
    buffer[j+3] = '#';
    if(segPtr->header.ack_num == 134600)
	{
	int i = 0;
	for(i = 0 ; i < 1504;i++)
	{
		printf("%d ",buffer[i]);
	}
	}
	printf("\n");
    if(send(connection,buffer,sizeof(buffer),0)<=0)
    {
        printf("send error!\n");
        return -1;
    }
    else
    {
        return 1;
    }
    
}

// 通过重叠网络(在本实验中，是一个TCP连接)接收STCP段. 我们建议你使用recv()一次接收一个字节.
// 你需要查找"!&", 然后是seg_t, 最后是"!#". 这实际上需要你实现一个搜索的FSM, 可以考虑使用如下所示的FSM.
// SEGSTART1 -- 起点 
// SEGSTART2 -- 接收到'!', 期待'&' 
// SEGRECV -- 接收到'&', 开始接收数据
// SEGSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 这里的假设是"!&"和"!#"不会出现在段的数据部分(虽然相当受限, 但实现会简单很多).
// 你应该以字符的方式一次读取一个字节, 将数据部分拷贝到缓冲区中返回给调用者.
//
// 注意: 还有一种处理方式可以允许"!&"和"!#"出现在段首部或段的数据部分. 具体处理方式是首先确保读取到!&，然后
// 直接读取定长的STCP段首部, 不考虑其中的特殊字符, 然后按照首部中的长度读取段数据, 最后确保以!#结尾.
//
// 注意: 在你剖析了一个STCP段之后,  你需要调用seglost()来模拟网络中数据包的丢失. 
// 在sip_recvseg()的下面是seglost(seg_t* segment)的代码.
//
// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 
int sip_recvseg(int connection, seg_t* segPtr)
{
   int finish = 1;
    while(finish)
    {	
        char temp = 0;
        recv(connection,&temp,sizeof(char),0);
		//printf("%d ",temp);
        if(temp=='`')
        {
                recv(connection,&temp,sizeof(char),0);
				//printf("%d ",temp);
                if(temp == '&')
                {
                    char buffer[1504];
                    memset(&buffer,0,1504);
					char temp2 = 0;
					int k = 0;
					int i = 0;
					for(k = 0;k < 24;k++)
					{
						recv(connection,&temp2,sizeof(char),0);
					//	printf("%d ",temp2);
						buffer[i] = temp2;
						i++;
					}
                    
                    char temp3 = temp2;
                    while(!(temp3 =='`'&&temp2 == '#'))
                    {
						temp3 = temp2;
                        recv(connection,&temp2,sizeof(char),0);
						
						
					//	printf("%d ",temp2);
                        if(!(temp2=='#'&&temp3=='`')&&i<1504)
                        {
                            buffer[i] = temp2;
                            i++;
							
                        } 
                        else
                        {
                            if(i>=1504)
                            {
                                printf("too more in seg!\n");
                             }
                        } 
                    }
                    //recv(connection,&temp2,sizeof(char),0);
					//printf("%d ",temp2);
                    if(temp2 == '#')
                    { 
                       
                        printf("receive one packet!\n");
                        memcpy(segPtr,buffer,sizeof(seg_t));
					if(seglost(segPtr))

                        {
							
                            printf("lose packet!\n");
                            return 1;
                        } 
						
						
						if(checkchecksum(segPtr) == -1)
					{
							printf("checksum error lose packet\n");
							return 1;
						}
					//		return 1;
						
                        return 0;
                    }
                    else
                    {
                        printf("\nno nomal '#' in tail\n");
						sleep(10);
                        //return 0;
                        
                    } 
                }
                else{
                    printf("\nno nomal '&' in head\n");
					sleep(10);
					exit(1);
                    //return 0;
                } 
        }
        
    }
    return 0;
}

int seglost(seg_t* segPtr) {
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		//50%可能性丢失段
		if(rand()%2==0) {
			printf("seg lost!!!\n");
      return 1;
		}
		//50%可能性是错误的校验和
		else {
			//获取数据长度
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
		//	//获取要反转的随机位
			int errorbit = rand()%(len*8);
			//反转该比特
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			return 0;
		}
	}
	return 0;
}

//这个函数计算指定段的校验和.
//校验和覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
	segment->header.checksum = 0;
	int mychecksum = sizeof(stcp_hdr_t) + strlen(segment->data);
	//printf("%d\n",mychecksum);
	unsigned char *temp  = (unsigned char*)malloc(mychecksum);
	memcpy(temp,segment,mychecksum);
	/*if(((int)checksum/2 )* 2 != checksum)
	{
		checksum++;
		char *temp = (char *)malloc(checksum);
		memset(temp,0,checksum);
		memcpy(temp,(char *)segment,checksum);
	}
	else
	{
		char *temp = (char *)malloc(checksum);
		memset(temp,0,checksum);
		memcpy(temp,(char *)segment,checksum);
	}
	*/
	unsigned long cksum=0;
	while(mychecksum>1)
	{
	cksum+=*(unsigned short int *)temp;
	temp +=2;
	//printf("%d\n",cksum);
	mychecksum-=sizeof(unsigned short int);
	}
	if(mychecksum)
	{
	cksum+=*(unsigned char *)temp;
	}
	while (cksum>>16)
		cksum=(cksum>>16)+(cksum & 0xffff);
	printf("cksum %d\n",(unsigned short int)(~cksum));
	return (unsigned short int)(~cksum);
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1
int checkchecksum(seg_t* segment)
{       
		int checksum = sizeof(stcp_hdr_t) + strlen(segment->data);
		//printf("checkchecksum! %d\n",checksum);
		//printf("header.check %d\n",segment->header.checksum);
	    unsigned char *temp = (unsigned char*)malloc(checksum);
		memcpy(temp,segment,checksum);
		//printf("temp->checksum: %d\n", *(unsigned short int*)(temp + 22));
	/*if(((int)checksum/2 )* 2 != checksum)
	{
		checksum++;
		char *temp = (char *)malloc(checksum);
		memset(temp,0,checksum);
		memcpy(temp,(char *)segment,checksum);
	}
	else
	{
		char *temp = (char *)malloc(checksum);
		memset(temp,0,checksum);
		memcpy(temp,(char *)segment,checksum);
	}
	*/
	unsigned long cksum=0;
	while(checksum>1)
	{
	//printf("!%d\n",*(unsigned short int*)temp);
	cksum+=*(unsigned short int *)temp;
	temp += 2;
	//printf("%d\n",cksum);
	checksum-=sizeof(unsigned short int);
	}
	if(checksum)
	{
	cksum+=*(unsigned char *)temp;
	}
	
	while (cksum>>16)
		cksum=(cksum>>16)+(cksum & 0xffff);
	printf("checkchecksum %d\n",~cksum);
  	if( (unsigned short)(~cksum) != 0 )
	{
		// TODO:
		printf("check sum error!\n");

		return -1;
		//return 0;
	}
	else
	{
		return 0;
	}
}
