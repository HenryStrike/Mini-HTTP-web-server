#include <iostream>
#include <libgen.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/epoll.h>
#include <csignal>
#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"

#define MAX_FD 65535 // 最大文件描述符数量
#define MAX_EVENT_NUM 10000 // 最大监听事件数

// 添加信号
void AddSig(int sig, void(handler)(int)) {
    struct sigaction sa{};
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

// 添加删除修改文件描述符
extern void AddFd(int epoll_fd, int fd, bool one_shot);

extern void RemoveFd(int epoll_fd, int fd);

extern void ModFd(int epoll_fd, int fd, int ev);

int main(int argc, char *argv[]) {

    if (argc <= 1) {
        printf("Input formay: %s port number\n", basename(argv[0]));
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);
    // 对信号进行处理
    AddSig(SIGPIPE, SIG_IGN);
    // 创建线程池
    ThreadPool<HttpConn> *thread_pool = nullptr;
    try {
        thread_pool = new ThreadPool<HttpConn>(8, 10000);
    } catch (...) {
        exit(-1);
    }

    // 创建连接池
    auto *users = new HttpConn[MAX_FD];

    // 创建监听的socket
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);

    // 设置端口复用
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 绑定端口号
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 所有IP都可以访问
    address.sin_port = htons(port);
    if (bind(listen_fd, (struct sockaddr *) &address, sizeof(address)) == -1) {
        printf("Bind failed\n");
        exit(-1);
    }

    // 监听socket
    listen(listen_fd, 5);

    // 创建epoll对象，事件数组
    struct epoll_event events[MAX_EVENT_NUM];
    int epoll_fd = epoll_create(5);

    // 将监听文件加入epoll事件 (epoll里只有这一个fd，暂时没有其他端口进入)
    AddFd(epoll_fd, listen_fd, false);
    HttpConn::m_epoll_fd = epoll_fd;

    while (true) {
        int event_num = epoll_wait(epoll_fd, events, MAX_EVENT_NUM, -1); // 事件发生数量
        if (event_num < 0 && errno != EINTR) { // 中断信号
            printf("Epoll failed\n");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < event_num; i++) {
            int sock_fd = events[i].data.fd;
            if (sock_fd == listen_fd) {
                // 监听到有客户端连接
                struct sockaddr_in client_address{};
                socklen_t client_address_len = sizeof(client_address);
                // 接收连接请求
                int conn_fd = accept(listen_fd, (struct sockaddr *) &client_address, &client_address_len);

                if (HttpConn::m_user_num >= MAX_FD) { // 目前连接数到达上限
                    close(conn_fd);
                    continue;
                }

                users[conn_fd].AddConn(conn_fd, client_address); // 将新客户加入连接池
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 发生错误，异常断开
                users[sock_fd].CloseConn();
            } else if (events[i].events & EPOLLIN) { // 收到信息，一次性读完
                if (users[sock_fd].Read()) {
                    thread_pool->Append(users + sock_fd);
                } else {
                    users[sock_fd].CloseConn();
                }
            } else if (events[i].events & EPOLLOUT) { // 写信息，一次性写完
                if (!users[sock_fd].Write()) {
                    users[sock_fd].CloseConn();
                }
            } else {
                printf("Unknown event\n");
                continue;
            }
        }
    }

    close(epoll_fd);
    close(listen_fd);
    delete[] users;
    delete thread_pool;

    return 0;
}
