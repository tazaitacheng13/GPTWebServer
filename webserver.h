#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <iostream>
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

#define MAX_FD 65535            // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000  //最大监听数量
#define TIMESLOT 20             // 最小超时单位  

class WebServer
{
public:
    WebServer();
    ~WebServer();
    void init(int port, string user, string password, string database, int log_write,
    int trignmode, int thread_num, int close_log);
    void trig_mode();
    void logwrite();
    void dbConn();
    void threadpool_create();
    void eventListen();
    void eventLoop();


public:
    // 连接信息
    int m_port;
    int m_log_write;
    int m_close_log;
    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    // 数据库相关
    MyDB* m_db;
    string m_user;
    string m_password;
    string m_database;
    
    // 线程池
    std::unique_ptr<threadpool<http_conn>> m_pool;
    int m_thread_num;

    // epoll相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_TrigMode;
    int m_ListenTrigMode;
    int m_ConntTrigMode;

    // 定时器
    util_timer* users_timer;
    Utils utils;
};


#endif
