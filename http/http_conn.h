#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>
#include <string>
#include <fstream>
#include "../include/json.hpp"

#include "../lock/locker.h"
#include "../http/http_conn.h"
#include "../db/db.h"
#include "../timer/lst_timer.h"
#include "Python.h"

using json = nlohmann::json; // json处理库

class http_conn {
public:
    static int m_epollfd; // 所有socket上的事件都被注册到同一个epoll上
    static int m_user_count; // 统计用户的数量
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 4096;
    static const int USERNAME_MAXLENGTH = 16;
    static const int PASSWARD_MAXLENGTH = 18;
    static const int MESSAGE_LENGTH = 1024;
    static const int RESPONSE_LENGTH = 4096;
    // 数据库
    static MyDB* m_db;
    static void init_db(MyDB* db) {
        m_db = db;
    };

    // HTTP请求的方法，但我们只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    // 解析客户端请求时，主状态机的状态
    // 当前正在分析请求行，分析头部字段，解析请求体
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    // 状态机的三种可能状态，即行的读取状态，分别表示
    // 读取到一个完整行，行出错，行数据尚且不完整
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    // 报文解析结果 
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, 
    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION, MESSAGE_REQUEST};

    http_conn() {}
    ~http_conn() {}
    void process();  // 处理客户端的请求
    void init(int sockfd, const sockaddr_in & addr);
    void close_conn(); // 关闭连接
    bool read(); // 非阻塞的读
    bool write(); // 非阻塞的写

    // 被process_write调用以填充HTTP应答内容
    bool process_write( HTTP_CODE ret );
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type(const char* s);
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    int m_sockfd; // 该HTTP连接的socket
    util_timer* timer; // 定时器
private:
    sockaddr_in m_address; // 通信的socket地址
    
    // 读相关变量
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_idx; // 标识读缓冲区中已经读入的客户端数据的最后一个字节的一下字节

    // 写相关变量
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx;          // 写缓冲区中待发送的字节数
    struct stat m_file_stat;  // 目标文件状态，判断请求是否合法
    char * m_file_address;    // 客户请求的目标文件被mmap到内存中的起始位置
    int m_iv_count;    // 
    struct iovec m_iv[2];    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int bytes_to_send;              // 将要发送的数据的字节数
    int bytes_have_send;            // 已经发送的字节数


    // 解析http相关变量
    int m_checked_index; // 当前正在分析的字符在读缓冲区的位置
    int m_start_line; // 当前正在解析的行的起始位置
    char m_real_file[ FILENAME_LEN ]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char * m_url; // 请求目标文件的文件名
    char * m_version; // 协议版本，支持HTTP1.1
    METHOD m_method; // 请求方法
    char * m_host; // 主机名
    bool m_linger; // 判断http请求是否要保持连接
    int m_content_length;
    int m_checked_idx = 2;
    char m_body[128]; // 存储请求体
    char m_username[USERNAME_MAXLENGTH]; // 存储解析出的用户名
    char m_password[PASSWARD_MAXLENGTH]; // 存储解析出的密码

    CHECK_STATE m_check_state; // 主状态机当前所处的状态

    void init(); // 初始化连接其余的信息
    HTTP_CODE process_read(); // 解析HTTP请求
    HTTP_CODE parse_request_line(char* text); // 解析请求首行
    HTTP_CODE parse_headers(char* text); // 解析头
    HTTP_CODE parse_content(char* text); // 解析请求体
    HTTP_CODE do_request();

    LINE_STATUS parse_line();
    char * get_line() {return m_read_buf + m_start_line;}

    // 数据库操作
    bool save_userinfo(); // 保存用户名和密码到数据库
    bool find_userinfo(); // 根据用户名和密码查找用户数据

    // 聊天信息
    char m_mess[MESSAGE_LENGTH];
    char m_response[RESPONSE_LENGTH];
    char m_content_type[50];
    json response_body;

    // 文件锁
    locker m_lock;
};

#endif