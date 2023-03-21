/*
 * @Author: fs1n
 * @Email: fs1n@qq.com
 * @Description: {To be filled in}
 * @Date: 2023-03-21 14:37:30
 * @LastEditors: fs1n
 * @LastEditTime: 2023-03-22 01:38:05
 */
#include "http_conn.h"

const char* OK_200_TITLE = "OK";
const char* ERROR_400_TITLE = "Bad Request";
const char* ERROR_400_FORM = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* ERROR_403_TITLE = "Forbidden";
const char* ERROR_403_FORM = "You do not have permission to get file from this server.\n";
const char* ERROR_404_TITLE = "Not Found";
const char* ERROR_404_FORM = "The requested file was not found on this server.\n";
const char* ERROR_500_TITLE = "Internal Error";
const char* ERROR_500_FORM = "There was an unusual problem serving the requested file.\n";

const char* DOC_ROOT = "/home/ubuntu/project/cppproject/nkWebServer/resources";

int http_conn::m_epollfd = -1;
int http_conn::m_usercount = 0;

/**
 * @brief 设置文件为非阻塞状态
 * @param {int} fd 文件标识符
 * @return 设置前文件状态标记
 */
int setnonblocking(int filefd){
    int old_flag = fcntl(filefd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(filefd, new_flag);
    return old_flag;
}

/**
 * @brief 添加需要监听的文件到 epoll 中
 * @param {int} epollfd epoll标识符
 * @param {int} filefd 文件描述符
 * @param {bool} one_shot 保证socket只被一个线程处理?
 */
void addfd(int epollfd, int filefd, bool one_shot){
    epoll_event event;
    event.data.fd = filefd;
    event.events = EPOLLIN | EPOLLRDHUP;        // ?水平触发
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, filefd, &event);
    setnonblocking(filefd);
}

/**
 * @brief 移除文件描述符
 * @param {int} epollfd epoll标识符
 * @param {int} filefd 文件描述符
 */
void removefd(int epollfd, int filefd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, filefd, 0);
    close(filefd);
}

/**
 * @brief 修改 socket 状态标记
 * @param {int} epollfd epoll标识符
 * @param {int} fd socked 标识符
 * @param {int} ev 状态标记
 */
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/**
 * @brief 
 * @return {*}
 */
void http_conn::close(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_usercount;
    }
}

/**
 * @brief 
 * @param {int} sockfd
 * @param {sockaddr_in} &addr
 * @return {*}
 */
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_addr = addr;

    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, m_sockfd, true);
    m_usercount++;

    init();
}

/**
 * @brief 
 * @return {*}
 */
void http_conn::init(){
    m_check_state = CHECK_STATE::REQUESTLINE;

    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_linger = false;   // 默认不保持连接

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

/**
 * @brief 
 * @return {*}
 */
bool http_conn::read(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }

    int bytes_read = 0;

    while(true){
        // (sockfd, 接收信息开始存放的地址, 最大接受字节数, 0)
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            return false;
        }else if(bytes_read == 0){
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/**
 * @brief 解析请求
 * @return HTTP 请求状态码
 */
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_STATUS::OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;

    char* text = 0;
    while( 
        (m_check_state == CHECK_STATE::CONTENT) &&          // 处于解析请求体状态
        (line_status == LINE_STATUS::OK) ||                 // 在正常读取状态
        ((line_status = parse_line()) == LINE_STATUS::OK)){ // 解析行正常状态
            text = getline();
            m_start_line = m_checked_idx;

            std::cout << "Read a http line: " << text << std::endl;

            switch (m_check_state)
            {
            case CHECK_STATE::REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == HTTP_CODE::BAD_REQUEST) return HTTP_CODE::BAD_REQUEST;
                break;
            }
            case CHECK_STATE::HEADER:{
                ret = parse_header(text);
                if(ret == HTTP_CODE::BAD_REQUEST) return HTTP_CODE::BAD_REQUEST;
                else if(ret == HTTP_CODE::GET_REQUEST) return do_request();         // 获取完整请求头，无 content
                break;
            }
            case CHECK_STATE::CONTENT:{
                ret = parse_content(text);
                if(ret == HTTP_CODE::BAD_REQUEST) return HTTP_CODE::BAD_REQUEST;
                else if(ret == HTTP_CODE::GET_REQUEST) return do_request();
                line_status = LINE_STATUS::OPEN;
                break;
            }
            default:
                return HTTP_CODE::INTERNAL_ERROR;
            }
    }
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 解析首行，获取 {Method, Target URL, HTTP version}
 *          GET /index.html HTTP/1.1
 * @param {char*} text 首行
 * @return HTTP 状态
 * @test TODO
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    // 解析 & 校验 Method
    char* itr = strpbrk(text, " ");
    char* method;
    int index = itr - text;
    strncpy(method, text, index);
    if(!method) return HTTP_CODE::BAD_REQUEST;
    method[index] = '\0'; // 末尾置为结束符
    if(strcasecmp(method, "GET") == 0) m_method = METHOD::GET;
    else return HTTP_CODE::BAD_REQUEST;

    // 解析 URL
    text = itr + 1;
    itr = strpbrk(text, " ");
    index = itr - text;
    strncpy(m_url, text, index);
    if(!m_url) return HTTP_CODE::BAD_REQUEST;
    m_url[index] = '\0';

    // 解析 & 校验 HTTP Version
    text = itr + 1;
    strcpy(m_version, text);
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return HTTP_CODE::BAD_REQUEST;

    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;                     // 去掉 "http://"
        m_url = strchr(m_url, '/');     // 跳转到第一个 '/' 后面，即略过 域名 or ip:port
    }
    m_check_state = CHECK_STATE::HEADER;
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 解析 HTTP请求头的一行, 请求头中信息为多行，每行为 [ Key : Value ]
 * @param {char*} text 请求头中的一行
 * @return HTTP 状态码
 * @test TODO
 */
http_conn::HTTP_CODE http_conn::parse_header(char* text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE::CONTENT;
            return HTTP_CODE::NO_REQUEST;
        }
        return HTTP_CODE::GET_REQUEST;
    }else if(strncasecmp(text, "Connection:", 11) != 0){
        text += 11;
        text += strspn(text, " ");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }else if(strncasecmp(text, "Content-Length:", 15) != 0){
        text += 15;
        text += strspn(text, " ");
        m_content_length = atol(text);
    }else if(strncasecmp(text, "Host:", 5) != 0){
        text += 5;
        text += strspn(text, " ");
        m_host = text;
    }
    else{
        std::cout << "unkown header: " << text << std::endl;
    }
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 
 * @param {char*} text
 * @return {*}
 */
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx >= m_checked_idx + m_content_length){
        text[m_content_length] = '\0';      // ?为什么要这样
        return HTTP_CODE::GET_REQUEST;
    }
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 解析单独一行，行结束为 "\r\n" ?为什么要这样操作
 * @param {char*} text
 * @return {*}
 * @test TODO
 */
http_conn::LINE_STATUS http_conn::parse_line(){
    while(m_checked_idx < m_read_idx){
        if(m_read_buf[m_checked_idx] == '\r'){
            if(m_checked_idx + 1 == m_read_idx){        // 回车后面还未读入，为 OPEN 状态
                return LINE_STATUS::OPEN;
            }else if(m_read_buf[m_checked_idx + 1] == '\n'){
                // 转换 "\r\n" 为 "\0\0"
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_STATUS::OK;
            }
            return LINE_STATUS::BAD;
        }
        m_checked_idx++;
    }
    return LINE_STATUS::OPEN;
}

/**
 * @brief 
 * @return {*}
 */
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, DOC_ROOT);
    int len = strlen(DOC_ROOT);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);      // -1 是给 '\0' 留空间
    
    // 不存在文件
    if(stat(m_real_file, &m_file_stat) < 0) return HTTP_CODE::NO_RESOURCE;

    // 禁止访问
    if(!(m_file_stat.st_mode & S_IROTH)) return HTTP_CODE::FORBIDDEN_REQUEST;

    if(S_ISDIR(m_file_stat.st_mode)) return HTTP_CODE::BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    return HTTP_CODE::FILE_REQUEST;
}

/**
 * @brief 释放内存中读入的文件
 * @return {*}
 */
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/**
 * @brief 
 * @return {*}
 */
bool http_conn::write(){
    int tmp = 0;
    int bytes_have_send = 0;
    int bytes_ready_send = m_write_idx;

    // 整体响应结束，初始化参数
    if(bytes_ready_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(true){
        tmp = writev(m_sockfd, m_iv, m_iv_count);
        if(tmp < 0){
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += tmp;
        bytes_ready_send -= tmp;
        if(bytes_ready_send <= bytes_have_send){
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }else{
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

/**
 * @brief 
 * @param {char*} format
 * @return {*}
 */
bool http_conn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx - 1, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - m_write_idx - 1) || len < 0) return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/**
 * @brief 
 * @param {int} status
 * @param {char*} title
 * @return {*}
 */
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/**
 * @brief 
 * @param {int} content_len
 * @return {*}
 */
bool http_conn::add_headers(int content_len){
    return  add_content_length(content_len)&&
            add_content_type()&&
            add_linger()&&
            add_blank_line();
}

/**
 * @brief 
 * @param {int} content_len
 * @return {*}
 */
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

/**
 * @brief 
 * @return {*}
 */
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
}

/**
 * @brief 
 * @return {*}
 */
bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close");
}

/**
 * @brief 
 * @return {*}
 */
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

/**
 * @brief 
 * @param {char*} content
 * @return {*}
 */
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}

/**
 * @brief 
 * @param {HTTP_CODE} ret
 * @return {*}
 * @todo 修改代码判 false 条件
 */
bool http_conn::process_write(HTTP_CODE ret){
    int tmp = m_write_idx;
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, ERROR_500_TITLE);
            add_headers( strlen(ERROR_500_FORM));
            if (!add_content( ERROR_500_FORM)){
                m_write_idx = tmp;
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, ERROR_400_TITLE);
            add_headers(strlen(ERROR_400_FORM));
            if (!add_content(ERROR_400_FORM)){
                m_write_idx = tmp;
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, ERROR_404_TITLE);
            add_headers(strlen(ERROR_404_FORM));
            if (!add_content(ERROR_404_FORM)){
                m_write_idx = tmp;
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, ERROR_403_TITLE);
            add_headers(strlen(ERROR_403_FORM));
            if (!add_content(ERROR_403_FORM)){
                m_write_idx = tmp;
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, OK_200_TITLE);
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/**
 * @brief 
 * @return {*}
 */
void http_conn::process(){
    // 解析 HTTP 请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == HTTP_CODE::NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN); 
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}