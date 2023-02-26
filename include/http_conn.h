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
#include <cstring>

// Http 任务类
class HttpConn {
public:
    static int m_epoll_fd; // 注册到同一个epoll当中

    static int m_user_num; // 用户数量

    static const size_t READ_BUFFER_SIZE = 2048; // 读缓冲大小

    static const size_t WRITE_BUFFER_SIZE = 1024; // 写缓冲大小

    static const int FILENAME_LEN = 200;        // 文件名的最大长度

    // HTTP请求方法，这里只支持GET
    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
    };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS {
        LINE_OK = 0, LINE_BAD, LINE_OPEN
    };

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
    int m_read_idx; // 记录当前读缓冲器最后一个字符的下一个位置
    int m_checked_idx; // 当前在parse的缓冲区的位置
    int m_start_line; // 行数据起始位置

    char *m_url; // 请求目标文件名
    char m_real_file[FILENAME_LEN]; // 请求的目标文件的完整路径，等于 doc_root + m_url, doc_root是网站根目录
    char *m_version; // 协议版本
    char *m_host; // 主机名
    bool m_linger; // 是否要保持连接
    METHOD m_method; // 请求方法
    int m_content_length;
    CHECK_STATE m_check_state; // 当前主机状态

    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx; // 写缓冲区中待发送的字节数
    char *m_file_address; // 请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat; // 目标文件的状态
    struct iovec m_iv[2]; // 采用writev来执行写操作，定义下面两个成员，其中m_iv_count表示被写内存块的数量
    int m_iv_count;

    int bytes_to_send; // 将要发送的数据的字节数
    int bytes_have_send; // 已经发送的字节数

    void Init(); // 初始化请求

    LINE_STATUS ParseLine();

    HTTP_CODE ProcessRead();

    bool ProcessWrite(HTTP_CODE read_ret);

    HTTP_CODE ParseRequest(char *text);

    HTTP_CODE ParseHeader(char *text);

    HTTP_CODE ParseContent(char *text);

    HTTP_CODE DoRequest();

    char *GetLine();

    void Unmap();

    bool AddResponse(const char *format, ...);

    bool AddContent(const char *content);

    bool AddContentType();

    bool AddStatusLine(int status, const char *title);

    void AddHeaders(int content_length);

    bool AddContentLength(int content_length);

    bool AddLinger();

    bool AddBlankLine();
};

#endif //WEBSERVER_HTTP_CONN_H
