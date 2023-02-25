//
// Created by INDEX on 2023/2/24.
//

#include "http_conn.h"

int HttpConn::m_user_num = 0;

int HttpConn::m_epoll_fd = -1;


void AddFd(int epoll_fd, int fd, bool one_shot) {
    struct epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞，否则在读取不到数据时会堵塞，尤其是水平触发时
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

void RemoveFd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void ModFd(int epoll_fd, int fd, int ev) {
    struct epoll_event event{};
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; // 重置ONE_SHOT事件
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}


HttpConn::HttpConn() {

}

HttpConn::~HttpConn() {

}

void HttpConn::Process() {
    // 解析Http请求
    printf("A thread is parsing http request\n");
    // 生成响应

    // 重新注册fd，方便下一个线程进行处理
    ModFd(m_epoll_fd, m_conn_fd, EPOLLIN);
}

void HttpConn::AddConn(int conn_fd, const sockaddr_in &client_address) {
    m_conn_fd = conn_fd;
    m_address = client_address;

    // 添加epoll对象进行监听
    AddFd(m_epoll_fd, conn_fd, true);
    m_user_num++;
}

void HttpConn::CloseConn() {
    if (m_conn_fd != -1) {
        RemoveFd(m_epoll_fd, m_conn_fd);
        m_conn_fd = -1;
        m_user_num--;
    }
}

bool HttpConn::Read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_conn_fd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        } else if (bytes_read == 0) { // client关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }

    printf("Read data from %d, data:\n %s\n", m_address.sin_addr.s_addr, m_read_buf);
    return true;
}

bool HttpConn::Write() {
    printf("Write all data in one time\n");
    return true;
}