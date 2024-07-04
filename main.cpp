#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#define MAX_FD 65535 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //最大监听数量

// 添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot, bool et);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符，充值socket上的EPOLLONESHOT时间，确保下一次可读的时候,EPOLL可触发
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char * argv[]){
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
        exit(-1);
    }    

    // 获取端口号
    int port = atoi(argv[1]);


    // 对SIGPIE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        delete pool;
        exit(-1);
    }

    // 设置端口
    socklen_t reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    // 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // inet_pton(AF_INET, "47.108.186.120", &addr.sin_addr.s_addr);
    addr.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(listenfd, (sockaddr*)& addr, sizeof(addr));
    if (ret == -1) {
        perror("bind");
        delete pool;
        exit(-1);
    }

    // 监听
    ret = listen(listenfd, 8);
    if (ret == -1) {
        perror("listen");
        delete pool;
        exit(-1);
    }
    
    // 创建epoll对象，事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false, false);
    http_conn::m_epollfd = epollfd;

    while (1) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR) {
            printf("epoll failure!\n");
            break;
        }

        // 循环遍历
        for (int i = 0 ; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // 有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int connfd = accept(listenfd, (sockaddr*) &client_address, &client_len);
                if (http_conn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 告知客户端：服务器正忙
                    close(connfd);
                    continue;
                }

                // 将新客户数据初始化，放到客户数组中
                users[connfd].init(connfd, client_address);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    // 一次性把所有数据读完
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
}
