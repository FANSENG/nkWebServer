#include<cstdio>
#include<cstdlib>
#include<cstring>
#include<arpa/inet.h>
#include<unistd.h>
#include<cerrno>
#include<sys/epoll.h>
#include<csignal>
#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65535                // 最大描述符个数, 即最大服务客户端数量
#define MAX_EVENT_NUMBER 10000      // 监听的最大数量


/**
 * @brief 信号处理，添加信号捕捉
 * @param {int} sig 信号
 * @param {void (int)} handler 信号处理函数
 */
void addsig(int sig, void (handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    // 设置新的信号处理方式
    sa.sa_handler = handler;

    // 在使用 handler 处理信号时要屏蔽的的信号
    // 即 handler 处理信号时屏蔽所有信号
    // sigfillset 函数是将输入的值置为全部信号集
    sigfillset(&sa.sa_mask);

    // sig : 需要捕捉的信号类型
    // sa : 新的信号处理方式
    // nullptr : 用于输出先前的信号处理方式, 不需要获得就置为了 nullptr
    sigaction(sig, &sa, nullptr);
}

/**
 * @brief 将 fd 添加到 epoll中
 * @param {int} epollfd epoll描述符
 * @param {int} fd 描述符
 * @param {bool} one_shot 保证socket只被一个线程处理
 */
extern void addfd(int epollfd, int fd, bool one_shot);

/**
 * @brief 从 epoll 中删除 fd
 * @param epollfd epoll描述符
 * @param fd 描述符
 */
extern void removefd(int epollfd, int fd);

/**
 * @brief 修改 描述符状态
 * @param epollfd epoll描述符
 * @param fd 描述符
 * @param ev 描述符
 */
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]){
    if(argc < 2){
        std::cout << "未输入正确参数，期望格式如下" << std::endl;
        printf("%s {port}\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);

    // SIGPIPE : 往 读端被关闭的管道 或者 socket连接中写数据
    // SIG_IGN : 忽略 SIGPIPE 的信号，本项目中用于忽略向 socket 连接中写数据
    addsig(SIGPIPE, SIG_IGN);
    
    // 线程池
    threadpool<http_conn> *pool = NULL;
    try{
        pool = new threadpool<http_conn>();
    }catch(...) {
        return 1;
    }
    sleep(1);           // 增加了 sleep(1) 就能运行?
    // 保存所有 client 信息
    http_conn *users = new http_conn[MAX_FD];

    // 监听 socket 描述符
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);     // host to net short

    // 设置端口复用
    int resue = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &resue, sizeof(resue));
    bind(listenfd, (struct sockaddr*) &address, sizeof(address));
    // 监听 {监听socket, 最大监听数目?}
    listen(listenfd, 5);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true){
        // epollfd : epoll 描述符
        // events : 记录事件的具体信息，包括描述符、结果等
        // MAX_EVENT_NUMBER - 1 : 最大事件数量
        // -1 : 是否设置最长处理时间
        std::cout << "Waiting Connection..." << std::endl;
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER - 1, -1);

        // ! 这里有修改
        if(num < 0 && errno != EINTR){
            std::cout << "EPOLL FAILURE" << std::endl;
            break;
        }

        for(int i = 0; i < num; i++){
            // client 连接的 socket
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                struct sockaddr_in client_addr;
                socklen_t client_addrlen = sizeof(client_addr);

                int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_addrlen);
                if(connfd < 0){
                    printf("Errno in : %d\n", errno);
                    continue;
                }
                printf("Client socket fd is: %d\n", connfd);
                if(http_conn::m_usercount >= MAX_FD){
                    printf("The connection pool is full and the server is busy\n");
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_addr);
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN){
                printf("发生读事件\n");
                if(users[sockfd].read()){
                    // ?放入待处理队列
                    printf("读事件进入待处理队列\n");
                    pool->append(users + sockfd);
                }else{
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                printf("发生写事件\n");
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}