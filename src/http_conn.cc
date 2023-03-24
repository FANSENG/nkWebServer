/*
 * @Author: fs1n
 * @Email: fs1n@qq.com
 * @Description: {To be filled in}
 * @Date: 2023-03-21 14:37:30
 * @LastEditors: fs1n
 * @LastEditTime: 2023-03-24 12:37:55
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
 * @brief 设置 socket 为非阻塞状态
 * @param {int} socketfd socket标识符
 * @return 设置前 socket 状态标记
 */
int setnonblocking(int socketfd){
    int old_flag = fcntl(socketfd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(socketfd, F_SETFL, new_flag);
    return old_flag;
}

/**
 * @brief 添加需要监听的 socket 添加到 epoll 中
 * @param {int} epollfd epoll标识符
 * @param {int} socketfd 文件描述符
 * @param {bool} one_shot 保证 socket 只被一个线程处理
 */
void addfd(int epollfd, int socketfd, bool one_shot){
    epoll_event event;
    event.data.fd = socketfd;
    event.events = EPOLLIN | EPOLLRDHUP;        // ?水平触发
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &event);
    // 设置 socket 为非阻塞，异步处理
    setnonblocking(socketfd);
}

/**
 * @brief 移除 socket 描述符
 * @param {int} epollfd epoll标识符
 * @param {int} socketfd socket 描述符
 */
void removefd(int epollfd, int socketfd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, socketfd, 0);
    close(socketfd);
}

/**
 * @brief 修改 socket 状态标记
 * @param {int} epollfd epoll标识符
 * @param {int} socketfd socked 标识符
 * @param {int} ev 状态标记
 */
void modfd(int epollfd, int socketfd, int ev){
    epoll_event event;
    event.data.fd = socketfd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, socketfd, &event);
}

/**
 * @brief 关闭 socket 连接
 * @return None
 */
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_usercount--;
    }
}

/**
 * @brief 初始化连接
 * @param {int} sockfd socket连接标识符
 * @param {sockaddr_in} &addr client 地址
 * @return None
 */
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_addr = addr;

    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // !这里 addfd 应该是在 epoll 中添加 sockfd
    addfd(m_epollfd, m_sockfd, true);
    m_usercount++;

    init();
}

/**
 * @brief 初始化连接信息
 * @return None
 */
void http_conn::init(){
    bytes_to_send = 0;
    bytes_have_send = 0;

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
 * @brief 循环读数据，直到 无数据 或 client 关闭连接
 * @return None
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
    printf("get data: %s", m_read_buf);
    return true;
}

/**
 * @brief 解析请求
 * @return HTTP 请求状态码
 */
http_conn::HTTP_CODE http_conn::process_read(){
    std::cout << "begin process read" << std::endl;
    LINE_STATUS line_status = LINE_STATUS::OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;

    char* text = 0;
    while( 
        (m_check_state == CHECK_STATE::CONTENT) &&          // 处于解析请求体状态
        (line_status == LINE_STATUS::OK) ||                 // 在正常读取状态
        ((line_status = parse_line()) == LINE_STATUS::OK)){ // 将要解析的行状态正常
            text = getline();                               // 读入将要解析的行
            m_start_line = m_checked_idx;                   // 将读取标置为下一行

            std::cout << "Read a http line: " << text << std::endl;

            switch (m_check_state){
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
                    line_status = LINE_STATUS::OPEN;        // 未读取完
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
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    std::cout << "parse request line: " << text << std::endl;
    // 解析 & 校验 Method
    char* itr = strpbrk(text, " ");
    char* method = text;
    *itr = '\0';
    if(!method) return HTTP_CODE::BAD_REQUEST;
    if(strcasecmp(method, "GET") == 0) m_method = METHOD::GET;
    else return HTTP_CODE::BAD_REQUEST;

    
    // 解析 URL
    itr++;
    m_url = itr;
    itr = strpbrk(itr, " ");
    *itr = '\0';
    if(!m_url) return HTTP_CODE::BAD_REQUEST;

    
    // 解析 & 校验 HTTP Version
    itr++;
    m_version = itr;
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return HTTP_CODE::BAD_REQUEST;

    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;                     // 去掉 "http://"
        m_url = strchr(m_url, '/');     // 跳转到第一个 '/' 后面，即略过 域名 or ip:port
    }
    m_check_state = CHECK_STATE::HEADER;
    
    //// /*
    // @test-block begin
    {
        std::cout << "Method: " << method << std::endl;
        std::cout << "URL: " << m_url << std::endl;
        std::cout << "Version: " << m_version << std::endl;
    }
    // @test-block end
    // */
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 解析 HTTP请求头的一行, 请求头中信息为多行，每行为 [ Key : Value ]
 * @param {char*} text 请求头中的一行
 * @return HTTP 状态码
 * @test TODO
 */
http_conn::HTTP_CODE http_conn::parse_header(char* text){
    std::cout << "parse header: " << text << std::endl;
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE::CONTENT;
            return HTTP_CODE::NO_REQUEST;
        }
        return HTTP_CODE::GET_REQUEST;
    }else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " ");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " ");
        m_content_length = atol(text);
    }else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " ");
        m_host = text;
    }
    else{
        std::cout << "unkown header: " << text << std::endl;
    }

    // /*
    // @test-block begin
    {
        std::cout << "m_content_length: " << m_content_length << std::endl;
        std::cout << "text: " << text << std::endl;
    }
    // @test-block end
    // */
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 解析请求体，这里只去判断是否完整读入
 * @param {char*} text 完整请求体
 * @return None
 */
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    std::cout << "parse content: " << text << std::endl;
    if(m_read_idx >= (m_checked_idx + m_content_length)){
        text[m_content_length] = '\0';      // ?为什么要这样
        return HTTP_CODE::GET_REQUEST;
    }
    return HTTP_CODE::NO_REQUEST;
}

/**
 * @brief 用于解析前的检查，行结束为 "\\r\\n"，检查这一行是否完整
 *          即是否以 "\\r\\n" 结尾
 * @param {char*} text
 * @return None
 * @test TODO
 */
http_conn::LINE_STATUS http_conn::parse_line(){
    std::cout << "check parse line" << std::endl;
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
 * @brief 读取请求的文件到内存中
 * @return None
 */
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, DOC_ROOT);
    int len = strlen(DOC_ROOT);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);      // -1 是给 '\0' 留空间
    
    std::cout << "Get Flie: " << m_real_file <<std::endl;

    // 不存在文件
    if(stat(m_real_file, &m_file_stat) < 0){
        std::cout << "No Flie: " << m_real_file <<std::endl;
        return HTTP_CODE::NO_RESOURCE;
    }

    // 禁止访问
    if(!(m_file_stat.st_mode & S_IROTH)){
        std::cout << "Forbidan access: " << m_real_file <<std::endl;
        return HTTP_CODE::FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)){
        std::cout << "Not a Flie: " << m_real_file <<std::endl;
        return HTTP_CODE::BAD_REQUEST;
    } 

    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    return HTTP_CODE::FILE_REQUEST;
}

/**
 * @brief 释放内存中读入的文件
 * @return None
 */
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/**
 * @brief 发送响应的数据
 * @return {bool} 发送是否成功
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
        tmp = writev(m_sockfd, m_iv, m_iv_count);       // tmp : 写入TCP缓存的字节数
        if(tmp < 0){
            // TCP写缓存满了
            // 等待下一个 EPOLLOUT 事件
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += tmp;
        bytes_ready_send -= tmp;

        // 发送完成, 根据请求中的字段决定是否保持连接
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
 * @brief 将响应数据写入写缓存中
 * @param {char*} format 格式化字符串
 * @param ... 需要填入字符串的数据
 * @return {bool} 发送是否成功
 */
bool http_conn::add_response(const char* format, ...){
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx - 1, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - m_write_idx - 1) || len < 0) return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/**
 * @brief 写入状态行
 * @param {int} status 状态码(404,200,500等)
 * @param {char*} title 状态Title
 * @return {bool} 发送是否成功
 */
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/**
 * @brief 写入 header 到写缓存
 * @param {int} content_len 
 * @return {bool} 写入是否成功
 */
bool http_conn::add_headers(int content_len){
    return  add_content_length(content_len)&&
            add_content_type()&&
            add_linger()&&
            add_blank_line();
}

/**
 * @brief 写入 content长度 到写缓存
 * @param {int} content_len
 * @return {bool} 写入是否成功
 */
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

/**
 * @brief 写入 Content-type 到写缓存
 * @return {bool} 写入是否成功
 */
bool http_conn::add_content_type(){
    char *format_file = strrchr(m_real_file, '.');
    return add_response("Content-Type:%s\r\n", format_file == NULL ? "text/html" : (format_file + 1));
}

/**
 * @brief 写入 是否保持连接 到写缓存
 * @return {bool} 写入是否成功
 */
bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close");
}

/**
 * @brief 写入空行
 * @return {bool} 写入是否成功
 */
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

/**
 * @brief 写入 content 到写缓存
 * @param {char*} content 数据内容
 * @return {bool} 写入是否成功
 */
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}

/**
 * @brief 根据服务器处理请求的结果, 给出返回 Client 的内容
 * @param {HTTP_CODE} ret HTTP_CODE http状态
 * @return {bool} 返回请求是否成功
 * @todo 修改代码判 false 条件
 */
bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
    {
        case INTERNAL_ERROR:
            if (!(  add_status_line(500, ERROR_500_TITLE) &&
                    add_headers(strlen(ERROR_500_FORM)) &&
                    add_content( ERROR_500_FORM)
                )){
                return false;
            }
            break;
        case BAD_REQUEST:
            if (!(  add_status_line(400, ERROR_400_TITLE) &&
                    add_headers(strlen(ERROR_400_FORM)) &&
                    add_content(ERROR_400_FORM)
                )){
                return false;
            }
            break;
        case NO_RESOURCE:
            if (!(  add_status_line(404, ERROR_404_TITLE) && 
                    add_headers(strlen(ERROR_404_FORM)) &&
                    add_content(ERROR_404_FORM) 
                )){
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            if (!(  add_status_line(403, ERROR_403_TITLE) &&
                    add_headers(strlen(ERROR_403_FORM)) &&
                    add_content(ERROR_403_FORM)
                )){
                return false;
            }
            break;
        case FILE_REQUEST:
            if(!(   add_status_line(200, OK_200_TITLE) &&
                    add_headers(m_file_stat.st_size)
            )) return false;
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
 * @brief 由线程池workder调用,用于处理HTTP请求
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
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}