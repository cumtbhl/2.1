
// 本代码进行服务器端高并发测试，模拟大量客户端并发连接到服务器

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>

#define MAX_BUFFER		128
#define MAX_EPOLLSIZE	(384*1024)
#define MAX_PORT		20
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

int isContinue = 0;

//套接字 fd 设置为非阻塞模式，套接字的读写操作不会被阻塞
//如果没有数据可读或不能写数据时，操作会立即返回
static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}

//设置 SO_REUSEADDR，操作系统允许在 TIME_WAIT 状态期间新的客户端重新绑定该端口
static int ntySetReUseAddr(int fd) {
	int reuse = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
}


//argv: 指 向字符串指针类型 的 指针
int main(int argc, char** argv) {

	//传入服务器端的 ip 和 port
	if (argc <= 2) {
		printf("Usage: %s ip port\n", argv[0]);
		exit(0);
	}

	const char *ip = argv[1];
	int port = atoi(argv[2]);
	int connections = 0;
	char buffer[128] = {0};
	int i = 0, index = 0;

	struct epoll_event events[MAX_EPOLLSIZE];
	
	//为 epoll_fd 指定一个较大的缓冲区
	int epoll_fd = epoll_create(MAX_EPOLLSIZE);
	
	strcpy(buffer, " Data From MulClient\n");
		
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);

	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	while (1) {
		if (++index >= MAX_PORT) index = 0;
		
		struct epoll_event ev;
		int sockfd = 0;

		if (connections < 340000 && !isContinue) {
			sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd == -1) {
				perror("socket");
				goto err;
			}

			//sockfd 复用服务器端的 0-19端口号
			addr.sin_port = htons(port+index);

			if (connect(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
				perror("connect");
				goto err;
			}
			ntySetNonblock(sockfd);
			ntySetReUseAddr(sockfd);

			//对于每次新生成的一个客户端 sockfd
			//服务器端都会发送 "Hello Server: client --> %d\n" 给该客户端
			sprintf(buffer, "Hello Server: client --> %d\n", connections);
			send(sockfd, buffer, strlen(buffer), 0);

			ev.data.fd = sockfd;
			ev.events = EPOLLIN | EPOLLOUT;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
		
			connections ++;
		}

		//每连接了个 1000 个客户端时，或者客户端连接数到达上限时
		if (connections % 1000 == 999 || connections >= 340000) {
			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);

			//检查每连接 1000 个客户端所花费的时间
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			printf("connections: %d, sockfd:%d, time_used:%d\n", connections, sockfd, time_used);

			//处理当前已连接的 connections 个客户端的事件
			int nfds = epoll_wait(epoll_fd, events, connections, 100);
			for (i = 0;i < nfds;i ++) {
				int clientfd = events[i].data.fd;

				//如果客户端 clientfd 可写，则发送 "data from %d\n"
				if (events[i].events & EPOLLOUT) {
					sprintf(buffer, "data from %d\n", clientfd);
					send(sockfd, buffer, strlen(buffer), 0);
				} 

				//如果客户端 clienfd 可读
				else if (events[i].events & EPOLLIN) {
					char rBuffer[MAX_BUFFER] = {0};				
					ssize_t length = recv(sockfd, rBuffer, MAX_BUFFER, 0);

					//如果客户端发了数据，则打印从 clientfd 接收到的数据
					if (length > 0) {
						printf(" RecvBuffer:%s\n", rBuffer);

						//strcmp(str1, str2): 如果str1 = str2 ，则返回0
						//当某个客户端发来 "quit" 时，则设置isContinue = 0
						if (!strcmp(rBuffer, "quit")) {
							isContinue = 0;
						}
						
					} 
					//如果客户端主动断开连接，则打印 " Disconnect clientfd:%d\n"
					else if (length == 0) {
						printf(" Disconnect clientfd:%d\n", clientfd);
						connections --;
						close(clientfd);
					} 

					//如果 recv() 返回负值时，表示在接收数据时发生了错误
					else {
						if (errno == EINTR) continue;

						printf(" Error clientfd:%d, errno:%d\n", clientfd, errno);
						close(clientfd);
					}
				} 

				//如果客户端的事件类型既不包含 EPOLLOUT 也不包含 EPOLLIN
				//则表示这是一个未知的事件类型，或者发生了错误
				else {
					printf(" clientfd:%d, errno:%d\n", clientfd, errno);
					close(clientfd);
				}
			}
		}

		usleep(500);
	}

	return 0;

err:
	printf("error : %s\n", strerror(errno));
	return 0;
}