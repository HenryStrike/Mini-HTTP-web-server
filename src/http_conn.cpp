//
// Created by INDEX on 2023/2/24.
//

#include "http_conn.h"

int HttpConn::m_user_num = 0;
int HttpConn::m_epoll_fd = -1;

static const char *doc_root = "/home/index/WebServer/resources"; // 网站根目录
static const char *ok_200_title = "OK";
static const char *error_400_title = "Bad Request";
static const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
static const char *error_403_title = "Forbidden";
static const char *error_403_form = "You do not have permission to get file from this server.\n";
static const char *error_404_title = "Not Found";
static const char *error_404_form = "The requested file was not found on this server.\n";
static const char *error_500_title = "Internal Error";
static const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// epoll相关函数============================================================
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
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; // 重置ONE_SHOT事件
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}
//==========================================================================

HttpConn::HttpConn() {}

HttpConn::~HttpConn() {}

void HttpConn::Init() {
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_url = nullptr;
    m_method = GET;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_linger = false;

    memset(m_read_buf, 0, READ_BUFFER_SIZE);
    memset(m_write_buf, 0, READ_BUFFER_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);
}

HttpConn::LINE_STATUS HttpConn::ParseLine() {
    // 根据/r/n判断一行数据，然后解析，具体方法为替换成字符串结束符\0
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) { // 读到了末尾，但是没有换行符号
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx] + 1 == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OK;
}

HttpConn::HTTP_CODE HttpConn::ParseRequest(char *text) {
    m_url = strpbrk(text, "\t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        printf("Only support GET method\n");
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url, "\t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // 状态转移
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseHeader(char *text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseContent(char *text) {
    // 没有真正实现，只判断是否读入
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::DoRequest() {
    // 寻找资源，写给client
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

char *HttpConn::GetLine() {
    return m_read_buf + m_start_line; // 跳转到开始行
}

void HttpConn::Unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool HttpConn::AddResponse(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool HttpConn::AddStatusLine(int status, const char *title) {
    return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void HttpConn::AddHeaders(int content_len) {
    AddContentLength(content_len);
    AddContentType();
    AddLinger();
    AddBlankLine();
}

bool HttpConn::AddContentLength(int content_len) {
    return AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::AddLinger() {
    return AddResponse("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::AddBlankLine() {
    return AddResponse("%s", "\r\n");
}

bool HttpConn::AddContent(const char *content) {
    return AddResponse("%s", content);
}

bool HttpConn::AddContentType() {
    return AddResponse("Content-Type:%s\r\n", "text/html");
}

HttpConn::HTTP_CODE HttpConn::ProcessRead() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = nullptr;

    while ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) ||
           (line_status = ParseLine()) == LINE_OK) { // 收到一行数据，准备处理，数据保存在
        text = GetLine();
        printf("Parsing one line:\n%s\n", text);
        m_start_line = m_checked_idx;

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE:
                ret = ParseRequest(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:
                ret = ParseHeader(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return DoRequest();
                }
                break;
            case CHECK_STATE_CONTENT:
                ret = ParseContent(text);
                if (ret == GET_REQUEST) {
                    return DoRequest();
                }
                line_status = LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
        }

        return NO_REQUEST;
    }
}

bool HttpConn::ProcessWrite(HttpConn::HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR:
            AddStatusLine(500, error_500_title);
            AddHeaders(strlen(error_500_form));
            if (!AddContent(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            AddStatusLine(400, error_400_title);
            AddHeaders(strlen(error_400_form));
            if (!AddContent(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            AddStatusLine(404, error_404_title);
            AddHeaders(strlen(error_404_form));
            if (!AddContent(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            AddStatusLine(403, error_403_title);
            AddHeaders(strlen(error_403_form));
            if (!AddContent(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            AddStatusLine(200, ok_200_title);
            AddHeaders(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void HttpConn::Process() {
    // 解析Http请求
    printf("A thread is parsing http request\n");
    HTTP_CODE read_ret = ProcessRead();
    if (read_ret == NO_REQUEST) {
        ModFd(m_epoll_fd, m_conn_fd, EPOLLIN);
        return;
    }
    // 生成响应
    printf("A thread is generating http response\n");
    bool write_ret = ProcessWrite(read_ret);
    if (!write_ret) {
        CloseConn();
    }
    // 重新注册fd，方便下一个线程进行处理
    ModFd(m_epoll_fd, m_conn_fd, EPOLLOUT);
}

void HttpConn::AddConn(int conn_fd, const sockaddr_in &client_address) {
    m_conn_fd = conn_fd;
    m_address = client_address;

    // 添加epoll对象进行监听
    AddFd(m_epoll_fd, conn_fd, true);
    m_user_num++;

    Init();
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

    printf("Read data:\n %s\n", m_read_buf);
    return true;
}

bool HttpConn::Write() {
    int temp = 0;

    if (bytes_to_send == 0) {
        // 将要发送的字节为0，这一次响应结束。
        ModFd(m_epoll_fd, m_conn_fd, EPOLLIN);
        Init();
        return true;
    }

    while (true) {
        // 分散写
        temp = writev(m_conn_fd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                ModFd(m_epoll_fd, m_conn_fd, EPOLLOUT);
                return true;
            }
            Unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            // 没有数据要发送了
            Unmap();
            ModFd(m_epoll_fd, m_conn_fd, EPOLLIN);

            if (m_linger) {
                Init();
                return true;
            } else {
                return false;
            }
        }

    }
}