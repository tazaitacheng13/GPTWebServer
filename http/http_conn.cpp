#include "http_conn.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
MyDB* http_conn::m_db = nullptr;
sort_timer_lst timer_lst;

// 网站的根目录
const char* doc_root = "/home/Cplus/webserver/resources";

// 设置描述符非阻塞
int setnonblocking(int fd) {
    int old_lg = fcntl(fd, F_GETFL);
    int new_lg = old_lg | O_NONBLOCK;
    return fcntl(fd, F_SETFL, new_lg);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot, bool et) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    if (et) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
     // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 向epoll中移除文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT时间，确保下一次可读的时候,EPOLL可触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr) {
    m_address = addr;
    m_sockfd = sockfd;
    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true, true);
    m_user_count++; // 总用户数加一

    // 初始化解析信息
    init();
}

// 初始化解析信息
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;
    m_content_length = 0;

    bytes_to_send = 0;
    bytes_have_send = 0;

    bzero(m_body, 128);
    bzero(m_username, USERNAME_MAXLENGTH);
    bzero(m_password, PASSWARD_MAXLENGTH);
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
    bzero(m_mess, MESSAGE_LENGTH);
    bzero(m_response, RESPONSE_LENGTH);
    bzero(m_content_type, 50);
}

// 关闭连接
void http_conn::close_conn() {
    if (m_sockfd != -1) {
        printf("close fd: %d\n", m_sockfd);
        LOG_INFO("close fd: %d", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
    // 超时关闭的客户端，定时器交给定时器链表的成员去删除，这里不需要处理
}


// 循环读取数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    m_read_idx = 0;

    // 读取到的字节
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + bytes_read, READ_BUFFER_SIZE - bytes_read, MSG_DONTWAIT); // 读完了不要发送信号，避免信号处理
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            timer_lst.del_timer(timer); // 删除定时器
            return false;
        }
        else if (bytes_read == 0) {
            // 对方关闭连接
            timer_lst.del_timer(timer); // 删除定时器
            return false;
        }
        m_read_idx += bytes_read;
        // 调整定时器，增加生存时间
        if( timer ) 
        {
            time_t cur = time( NULL );
            timer->expire = cur + 3 * TIMESLOT;
            printf( "adjust timer once\n" );
            timer_lst.adjust_timer( timer );
        }
    }
    LOG_INFO("读取到了数据, 长度: %d \n %s\n", m_read_idx, m_read_buf);
    return true;
}

// 主状态机, 解析请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK )|| (line_status = parse_line()) == LINE_OK) {
        // 解析到了一行完整的数据，或者解析到了请求体，也是一行完整数据
        text = get_line();
        m_start_line = m_checked_index;
        LOG_INFO("got 1 http line: %s\n", text);

        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " ");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char * method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    } 
    else {
        return BAD_REQUEST;
    }
    m_version = strpbrk(m_url, " ");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:10000/index.html
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; // 192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    // 默认返回index.html
    if (strcmp(m_url, "/") == 0) {
        strcpy(m_url, "/index.html");
    }
    
    m_check_state = CHECK_STATE_HEADER; // 主状态机检查状态变为检查请求头
    return NO_REQUEST;
}

// 解析头
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求为POST，则处理消息体
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_method == POST ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        LOG_ERROR( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

// 解析请求体
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    strcpy(m_body, text);
    if ( strlen(m_body) < m_content_length )
    {
        return NO_REQUEST;
    }

    // 解析POST请求体
    std::string body = m_body;
    std::string::size_type pos = body.find("username=");
    if (pos != std::string::npos) {
        pos += strlen("username=");
        std::string::size_type end = body.find('&', pos);
        strcpy(m_username, body.substr(pos, end - pos).c_str());
    }

    pos = body.find("password=");
    if (pos != std::string::npos) {
        pos += strlen("password=");
        std::string::size_type end = body.find('&', pos);
        strcpy(m_password, body.substr(pos, end - pos).c_str());
    }

    // 解析json类型消息体
    if (body[0] == '{') {
        json parsed_json = json::parse(body);
        std::string mess = parsed_json["message"];
        strcpy(m_mess, mess.c_str());
    }
    
    return GET_REQUEST;
}

// 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (;m_checked_index < m_read_idx; ++m_checked_index) {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r') {
            if (m_checked_index + 1 == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n') {
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r') {
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
     // "/home/nowcoder/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    if (m_method == POST && strcmp(m_url, "/register_user.php") == 0) {
        if(save_userinfo()) {
            strcpy(m_url, "/regist_success.html");
        }
    }
    else if (m_method == POST && strcmp(m_url, "/login") == 0) {
        if (find_userinfo()) {
            strcpy(m_url, "/welcome.html");
        }
        else {
            strcpy(m_url, "/login_failed.html");
        }
    }
    else if (m_method == POST && strcmp(m_url, "/send-message") == 0) {
        // 收到客户端消息
        m_lock.lock();
        std::ofstream file_w("/home/Cplus/webserver-master/content.txt");
        if (!file_w.is_open()) {
            std::cout << "content文件打开失败" << std::endl;
        }
        file_w.write(m_mess, strlen(m_mess));
        file_w.close();
        m_lock.unlock();
        Py_Initialize();
        // 执行一个简单的执行python脚本命令
        // PyRun_SimpleString("print('hello world')\n");
        PyRun_SimpleString("exec(open('/home/Cplus/webserver/gpt/wenxin.py', encoding = 'utf-8').read())");
        // PyRun_SimpleString("exec(open('/home/Cplus/webserver/gpt/wenxin.py).read())");
        // 撤销Py_Initialize()和随后使用Python/C API函数进行的所有初始化
        Py_Finalize();

        m_lock.lock();
        std::ifstream file_r("/home/Cplus/webserver-master/response.txt");
        if (!file_r.is_open()) {
            std::cout << "无法打开文件" << std::endl;
        }
        std::stringstream m_buff;
        m_buff << file_r.rdbuf();
        strcpy(m_response, m_buff.str().c_str());
        file_r.close();
        m_lock.unlock();

        strcpy(m_content_type, "application/json");
        response_body["reply"] = m_response;
        strcpy(m_response, response_body.dump().c_str());
        return MESSAGE_REQUEST;
    }


    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 ); 
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


bool http_conn::write() {
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            timer_lst.del_timer(timer); // 删除定时器
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            init();
            if (m_linger)
            {
                return true;
            }
            else
            {
                return false;
            }
        }

    }
}



// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type(m_content_type);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type(const char* s) {
    return add_response("Content-Type:%s\r\n", s);
}


bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        case MESSAGE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(strlen(m_response));
            add_content(m_response);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv_count = 1;
            bytes_to_send = m_write_idx;
            return true;
        default:
            return false;
    }
}


// 线程池的工作线程调用，处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN); // 刷新one-shot设置，继续读取数据
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// 保存用户信息到数据库中
bool http_conn::save_userinfo() {
    std::string username = m_username;
    std::string password = m_password;
    std::string sql = "insert into users (username, password) values ('" + username + "', '" + password + "')";
    
    if (!http_conn::m_db->exeSQL(sql)) {
        std::cout << "Failed to execute SQL: " << sql << std::endl;
        return false;
    }
    std::cout << "User information saved successfully!" << std::endl;
    return true;
}

bool http_conn::find_userinfo() {
    std::string username = m_username;
    std::string password = m_password;
    std::string sql = "select * from users where username = '" + username + "' and password = '" + password + "'";

    if (!http_conn::m_db->exeSQL(sql)) {
        std::cout << "Falied to execute SQL: " << sql << std::endl;
        return false;
    }
    std::cout << "User information found!" << std::endl;
    return true;
}