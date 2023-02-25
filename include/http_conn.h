//
// Created by INDEX on 2023/2/24.
//

#ifndef WEBSERVER_HTTP_CONN_H
#define WEBSERVER_HTTP_CONN_H

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <sys/types.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstdarg>
#include <cerrno>
#include "locker.h"
#include <sys/uio.h>

// Http 任务类
class HttpConn {
public:
    static int m_epoll_fd; // 注册到同一个epoll当中

    static int m_user_num; // 用户数量

    static const size_t READ_BUFFER_SIZE = 2048; // 读缓冲大小

    static const size_t WRITE_BUFFER_SIZE = 1024;

    HttpConn();

    ~HttpConn();

    void Process(); // 处理请求

    void AddConn(int conn_fd, const sockaddr_in &client_address); // 初始化client信息

    void CloseConn(); // 关闭client连接

    bool Read();

    bool Write();

private:
    int m_conn_fd; // 连接的fd

    struct sockaddr_in m_address{}; // client地址

    char m_read_buf[READ_BUFFER_SIZE]; // 读缓存

    int m_read_idx; // 记录读取位置

};

#endif //WEBSERVER_HTTP_CONN_H
