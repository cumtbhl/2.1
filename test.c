#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#define MAX_CLIENTS 100

void *client_thread(void *arg)
{
    int clientfd = *(int *)arg;
    while (1)
    {
        char buffer[128] = {0};
        int count = recv(clientfd, buffer, 128, 0);
        if (count == 0)
        {
            break;
        }
        send(clientfd, buffer, count, 0);
        printf("clientfd: %d, count: %d, buffer: %s\n", clientfd, count, buffer);
    }
    close(clientfd);
    return NULL;
}

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(2048);

    if (-1 == bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr)))
    {
        perror("bind error!\n");
        return -1;
    }

    listen(sockfd, 10);

#if 0 // 阻塞io的方式
    while (1)
    {
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);

        int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);
        
        pthread_t thid;
        pthread_create(&thid, NULL, client_thread, &clientfd);
        pthread_detach(thid);
    }

#elif 0 // select()的方式
    fd_set rfds, rset;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);
    int maxfd = sockfd;

    while (1)
    {
        rset = rfds;
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (FD_ISSET(sockfd, &rset))
        {
            struct sockaddr_in clientaddr;
            socklen_t len = sizeof(clientaddr);

            int clientfd = accept(sockfd, (struct sockaddr *)&clientaddr, &len);
            printf("clientfd : %d connected!\n", clientfd);
            FD_SET(clientfd, &rfds);
            maxfd = clientfd;
        }

        int i = 0;
        for (i = sockfd + 1; i <= maxfd; i++)
        {
            if (FD_ISSET(i, &rset))
            {
                char buffer[128] = {0};
                int count = recv(i, buffer, 128, 0);
                if (count == 0)
                {
                    printf("clientfd : %d disconnect!\n", i);
                    FD_CLR(i, &rfds);
                    close(i);
                    break;
                }
                send(i, buffer, count, 0);
                printf("clientfd: %d, count: %d, buffer: %s\n", i, count, buffer);
            }
        }
    }

#elif 0 // poll()的方式
    // struct pollfd
    // {
    //     int fd;            /* File descriptor to poll.  */
    //     short int events;  /* Types of events poller cares about.  */
    //     short int revents; /* Types of events that actually occurred.  */
    // };
    struct pollfd fds[1024] = {0};
    fds[sockfd].fd = sockfd;
    fds[sockfd].events = POLLIN;
    int maxfd = sockfd;
    while (1)
    {
        int nready = poll(fds, maxfd + 1, -1);

        if (fds[sockfd].revents & POLLIN)
        {
            struct sockaddr_in clientaddr;
            socklen_t len = sizeof(clientaddr);

            int clientfd = accept(sockfd, (struct sockaddr *)&clientaddr, &len);
            printf("sockfd: %d connect!\n", clientfd);
            fds[clientfd].fd = clientfd;
            fds[clientfd].events = POLLIN;
            maxfd = clientfd;
        }

        int i = 0;
        for (i = sockfd + 1; i <= maxfd; i++)
        {
            if (fds[i].revents & POLLIN)
            {
                char buffer[128] = {0};
                int count = recv(i, buffer, 128, 0);
                if (count == 0)
                {
                    printf("clientfd : %d disconnect!\n", i);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    close(i);
                    continue;
                }
                send(i, buffer, count, 0);
                printf("clientfd: %d, count: %d, buffer: %s\n", i, count, buffer);
            }
        }
    }

#elif 1 //epoll()的方式
    //创建 1 个 epoll 实例，返回一个文件描述符 epfd
    int epfd = epoll_create(1);

    //ev：表示我们希望 epoll 监听的事件
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;

    //epoll_ctl()：将 sockfd 加入到 epfd 的监听队列中
    //epfd 将监听 sockfd 的 EPOLLIN 事件
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    //events：存储 epoll_wait() 返回的发生事件的文件描述符信息
    struct epoll_event events[1024] = {0};

    while(1){ 
        //epoll_wait()：等待 epfd 中注册的文件描述符上的事件发生
        int nready = epoll_wait(epfd, events, 1024, -1);
        int i = 0;
        for(i = 0; i < nready; i++){
            //connfd：发生事件的文件描述符
            int connfd = events[i].data.fd;

            if(sockfd == connfd){
                struct sockaddr_in clientaddr;
                socklen_t len = sizeof(clientaddr);
                int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = clientfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
                printf("clientfd: %d connect!\n", clientfd);
            }

            else if(events[i].events & EPOLLIN){
                char buffer[128] = {0};
                int count = recv(connfd, buffer, 128, 0);
                if(count == 0){
                    printf("clientfd : %d disconnect!\n", connfd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
                    close(connfd);
                    continue;
                }
                send(connfd, buffer, count, 0);
                printf("clientfd: %d, count: %d, buffer: %s\n", connfd, count, buffer);
            }
        }
    }

#endif
    close(sockfd);
    return 0;
}