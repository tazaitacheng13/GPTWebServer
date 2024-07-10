#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./timer/lst_timer.h"
#include "./db/db.h"
#include "./log/log.h"


#define MAX_FD 65535 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //最大监听数量


static int pipefd[2];
extern sort_timer_lst timer_lst;
// // 添加信号捕捉
// void addsig(int sig, void(handler)(int)) {
//     struct sigaction sa;
//     memset(&sa, '\0', sizeof(sa));
//     sa.sa_handler = handler;
//     sigfillset(&sa.sa_mask);
//     sigaction(sig, &sa, NULL);
// }

// 发送信号
void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, MSG_DONTWAIT);
    errno = save_errno;
}

// 添加定时器的ALARM信号
void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}


// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func( http_conn* user_data )
{
    printf( "close fd %d\n", user_data->m_sockfd );
    user_data->close_conn();
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

    // 初始化日志
    Log::get_instance()->init("./ServerLog", 1, 2000, 800000, 800);
    
    LOG_INFO("%s", "run server");
    // 获取端口号
    int port = atoi(argv[1]);


    // 对SIGPIPE信号进行处理  这里我设置了send MSG_DONTSIGNAL，不会发送SIGPIPE信号
    // addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    // 创建数据库用于检索和保存用户信息
    MyDB db;
    if (!db.initDB("127.0.0.1","root","123456","user_profile")) {
        printf("connect db error\n");
        exit(-1);
    };
    http_conn::init_db(&db); // 初始化客户端作业类的数据库指针，所有作业对象共享

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

    // 创建定时信号管道
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    addfd( epollfd, pipefd[0], false, false);

    // 设置信号处理函数
    addsig( SIGALRM );

    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

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
                    LOG_WARN("insufficent fd");
                    close(connfd);
                    continue;
                }

                // 将新客户数据初始化，放到客户数组中
                users[connfd].init(connfd, client_address, 1);
                LOG_INFO("new connect, fd: %d", connfd);

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer; // 关联定时器
                timer_lst.add_timer( timer );
            }
            else if (sockfd == pipefd[0] && events[i].events & EPOLLIN) {
                // 处理定时信号，清理非活跃连接
                int sig;
                char signal[1024];
                ret = recv(sockfd, signal, sizeof(signal), 0);
                if (ret == -1 || ret == 0) {
                    continue;
                }
                else if (ret > 0) {
                    switch (signal[0])
                    {
                    case SIGALRM:
                    {
                        timeout = true;
                    }
                    }
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                LOG_INFO("client closed or some errors happened");
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
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
}
