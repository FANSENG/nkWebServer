//
// Created by fs1n on 3/21/23.
//

#ifndef HTTP_COND_H
#define HTTP_COND_H

#include "locker.h"
#include <unistd.h>
#include <csignal>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <sys/stat.h>
#include <cstring>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <cstdarg>
#include <cerrno>
#include <sys/uio.h>

class http_conn{
public:
    static int m_epollfd;
    static int m_usercount;
    static const int READ_BUFFER_SIZE = 1 << 11;
    static const int WRITE_BUFFER_SIZE = 1 << 10;
    static const int FILENAME_LEN = 200;

    enum METHOD{
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    enum CHECK_STATE{
        REQUESTLINE = 0,    // 正在分析请求行
        HEADER,             // 正在分析头部字段
        CONTENT             // 正在解析请求体
    };

    enum LINE_STATUS{
        OK = 0,             // 读取到完整行
        BAD,                // 行出错
        OPEN                // 尚不完整
    };

    enum HTTP_CODE{
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    http_conn();
    ~http_conn();

    void init(int sockfd, const sockaddr_in &addr); // 初始化连接
    void close();                                   // 关闭连接
    void process();                                 // 处理 client 请求
    bool read();                                    // 非阻塞读
    bool write();                                   // 非阻塞写

private:
    void init();                                    // 初始化其他信息
    HTTP_CODE process_read();
    HTTP_CODE process_request_line(char* text);
    HTTP_CODE process_header(char* text);
    HTTP_CODE process_content(char* text);
    inline char* getline(){
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();
    LINE_STATUS parse_line();


    bool process_write(HTTP_CODE ret);              //填充HTTP应答
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
private:
    int m_sockfd;                                   // 用于连接的 sock
    sockaddr_in m_addr;                             // client 地址
    CHECK_STATE m_check_state;                      // 当前主机状态


    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    char m_read_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    METHOD m_method;
    char* m_host;
    int m_content_length;
    bool m_linger;


    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    char* m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif // HTTP_COND_H
