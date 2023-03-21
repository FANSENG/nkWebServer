//
// Created by fs1n on 3/21/23.
//

#ifndef HTTP_COND_H
#define HTTP_COND_H

#include "locker.h"
#include <iostream>
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
    static int m_epollfd;                       // 用一个 epoll 管理 socket
    static int m_usercount;                     // 用户数量
    static const int READ_BUFFER_SIZE = 1 << 11;
    static const int WRITE_BUFFER_SIZE = 1 << 10;
    static const int FILENAME_LEN = 200;        // 文件名最大长度

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
        REQUESTLINE = 0,                        // 正在分析请求行
        HEADER,                                 // 正在分析头部字段
        CONTENT                                 // 正在解析请求体
    };

    enum LINE_STATUS{
        OK = 0,                                 // 读取到完整行
        BAD,                                    // 行出错
        OPEN                                    // 尚不完整
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
    HTTP_CODE process_read();                       // 解析请求
    HTTP_CODE parse_request_line(char* text);       // 解析第一行
    HTTP_CODE parse_header(char* text);             // 解析头部
    HTTP_CODE parse_content(char* text);            // 解析body
    LINE_STATUS parse_line();                       // 解析单独一行
    inline char* getline(){                         // 获取当前正在解析行的起始地址
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();                         // 发送 request


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


    char m_read_buf[READ_BUFFER_SIZE];              // 读缓冲区
    int m_read_idx;                                 // 读指针，指向读入如最后一个字节的下标
    int m_checked_idx;                              // 解析报文时，正在读的字符位置
    int m_start_line;                               // 当前正在解析行的起始位置
    
    /*解析得到的信息*/
    char m_real_file[FILENAME_LEN];                 // 客户请求文件的绝对路径
    char* m_url;                                    // 解析得到的 url
    char* m_version;                                // 协议版本号(1.1)
    METHOD m_method;                                // 请求method
    char* m_host;                                   // client端 host
    long m_content_length;                          // 请求总长度
    bool m_linger;                                  // ?是否 keep alive


    char m_write_buf[WRITE_BUFFER_SIZE];            // 写缓冲区
    int m_write_idx;                                // 写缓冲区中待发送字节数
    char* m_file_address;                           // 客户请求文件读取到内存中的起始位置
    struct stat m_file_stat;                        // 目标文件状态


    struct iovec m_iv[2];                           // 使用writev
    int m_iv_count;                                 // 需要写的数量
};

#endif // HTTP_COND_H
