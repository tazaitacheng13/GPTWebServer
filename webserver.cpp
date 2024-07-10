#include "webserver.h"


WebServer::WebServer()
{
    // 创建一个数组用于保存所有的客户端信息
    users = new http_conn[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
}

void WebServer::init(int port, string user, string password, string database, int log_write,
                    int trigmode, int thread_num, int close_log)
{
    m_port = port;
    m_user = user;
    m_password = password;
    m_database = database;
    m_log_write = log_write;
    m_TrigMode = trigmode;
    m_thread_num = thread_num;
    m_close_log = close_log;
}



void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TrigMode)
    {
        m_ListenTrigMode = 0;
        m_ConntTrigMode = 0;
    }
    //LT + ET
    else if (1 == m_TrigMode)
    {
        m_ListenTrigMode = 0;
        m_ConntTrigMode  = 1;
    }
    //ET + LT
    else if (2 == m_TrigMode)
    {
        m_ListenTrigMode = 1;
        m_ConntTrigMode  = 0;
    }
    //ET + ET
    else if (3 == m_TrigMode)
    {
        m_ListenTrigMode = 1;
        m_ConntTrigMode  = 1;
    }
}


void WebServer::logwrite() {
    // 初始化日志
    if (m_log_write == 1)
        Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
    else
        Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
}


void WebServer::dbConn() {
    m_db = new MyDB();
    if (!m_db->initDB("localhost", m_user, m_password, m_database)) {
        printf("connect db error\n");
        exit(-1);
    };
    http_conn::init_db(m_db);
}


void WebServer::threadpool_create() {
    m_pool.reset(new threadpool<http_conn>());
}


void WebServer::eventListen() {
    // socket创建（网络编程基础流程）
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd == -1) {
        perror("socket");
        exit(-1);
    }

    // 设置端口
    socklen_t reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    // 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(m_listenfd, (sockaddr*)& addr, sizeof(addr));
    if (ret == -1) {
        perror("bind");
        exit(-1);
    }

    // 监听
    ret = listen(m_listenfd, 8);
    if (ret == -1) {
        perror("listen");
        exit(-1);
    }

    utils.init(TIMESLOT);

    // 创建epoll对象，事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);

    // 将监听的文件描述符添加到epoll对象中
    utils.addfd(m_epollfd, m_listenfd, false, m_ListenTrigMode);
    http_conn::m_epollfd = m_epollfd;

    // 创建定时信号管道
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert( ret != -1 );
    utils.addfd( m_epollfd, m_pipefd[0], false, false);

    // 设置信号处理函数
    utils.addsig( SIGALRM , utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

    // 初始化工具对象的文件描述符
    Utils::u_pipefd = m_pipefd;
}


void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server) {
        int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR) {
            LOG_ERROR("epoll failure!\n");
            break;
        }

        // 循环遍历
        for (int i = 0 ; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == m_listenfd) {
                // 有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int connfd = accept(m_listenfd, (sockaddr*) &client_address, &client_len);
                if (http_conn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 告知客户端：服务器正忙
                    LOG_WARN("insufficent fd");
                    close(connfd);
                    continue;
                }

                // 将新客户数据初始化，放到客户数组中
                users[connfd].init(connfd, client_address, m_ConntTrigMode);
                LOG_INFO("new connect, fd: %d", connfd);

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer; // 关联定时器
                utils.m_timer_lst->add_timer( timer );
            }
            else if (sockfd == m_pipefd[0] && events[i].events & EPOLLIN) {
                // 处理定时信号，清理非活跃连接
                int sig;
                char signal[1024];
                int ret = recv(sockfd, signal, sizeof(signal), 0);
                if (ret == -1 || ret == 0) {
                    continue;
                }
                else if (ret > 0) {
                    for (int i = 0; i < ret; ++i) {
                        switch (signal[0])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                            break;
                        }
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
                    m_pool->append(users + sockfd);
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
        // 最后处理定时事件，因为I/O事件有更高的优先级
        if( timeout ) {
            utils.timer_handler();
            timeout = false;
        }
    }
}