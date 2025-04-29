#include "sylar/streams/zlib_stream.h"
#include "http_connection.h"
#include "http_parser.h"
#include "sylar/util.h"
#include "sylar/log.h"

namespace sylar {
namespace http {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::string HttpResult::toString() const {
    std::stringstream ss;
    ss << result << " " << error << " " << response << std::endl;
    return ss.str();
}

HttpConnection::HttpConnection(Socket::ptr sock, bool owner) : SocketStream(sock, owner) {}

HttpConnection::~HttpConnection() {
    SYLAR_LOG_DEBUG(g_logger) << "HttpConnection::~HttpConnection";
}

/**
 * @brief 接收并解析HTTP响应
 * @return HttpResponse::ptr 解析成功的响应对象指针，失败返回nullptr
 * 
 * 完整处理流程：
 * 1. 初始化解析器和缓冲区
 * 2. 循环读取并解析响应头
 * 3. 处理分块传输或固定长度响应体
 * 4. 处理内容编码（如gzip压缩）
 * 5. 返回最终响应对象
 */
HttpResponse::ptr HttpConnection::recvResponse() {
    // 1. 初始化HTTP响应解析器和数据缓冲区
    HttpResponseParser::ptr parser(new HttpResponseParser);  // 创建响应解析器实例
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();  // 获取缓冲区大小配置值
    
    // 创建智能指针管理的缓冲区，带自定义删除器（自动释放内存）
    std::shared_ptr<char> buffer(new char[buff_size + 1], [](char* ptr) { 
        delete[] ptr; 
    });
    
    // 获取裸指针用于直接操作缓冲区数据
    char* data = buffer.get();
    // 记录已读取但未解析的数据偏移量（用于处理不完整的报文）
    int offset = 0;

    // 2. 主解析循环 - 处理响应头
    do {
        // 从socket读取数据到缓冲区（从offset位置开始写，避免覆盖未解析数据）
        int len = read(data + offset, buff_size - offset);
        if (len <= 0) {  // 读取失败或连接关闭
            close();     // 关闭连接
            return nullptr;
        }
        len += offset;   // 计算当前缓冲区有效数据总长度
        
        // 执行HTTP响应解析（false表示解析头部，不处理body）
        size_t nparse = parser->execute(data, len, false);
        if (parser->hasError()) {  // 解析错误处理
            close();
            return nullptr;
        }
        
        // 计算剩余未解析数据量（可能包含部分body数据）
        offset = len - nparse;
        if (offset == (int)buff_size) {  // 缓冲区溢出保护
            close();
            return nullptr;
        }
        if (parser->isFinshed()) {  // 头部解析完成
            break;
        }
    } while(true);

    // 3. 获取解析器底层状态机引用（用于检查传输编码等）
    auto& client_parser = parser->getParser();
    std::string body;  // 准备存储响应体

    // 4. 处理响应体（分块传输和固定长度两种方式）
    if (client_parser.chunked) {  // 分块传输编码处理
        int len = offset;  // 初始化未处理数据长度
        do {
            bool begin = true;  // 标记是否开始处理新分块
            do {
                // 需要读取更多数据的情况：不是第一个分块或缓冲区已空
                if(!begin || len == 0) {
                    int rt = read(data + len, buff_size - len);  // 读取数据
                    if(rt <= 0) {  // 读取失败
                        close();
                        return nullptr;
                    }
                    len += rt;  // 更新缓冲区有效长度
                }
                data[len] = '\0';  // 确保字符串终止
                
                // 执行分块数据解析（true表示处理body）
                size_t nparse = parser->execute(data, len, true);
                if(parser->hasError()) {  // 解析错误
                    close();
                    return nullptr;
                }
                len -= nparse;  // 减去已解析的字节数
                if(len == (int)buff_size) {  // 缓冲区溢出检查
                    close();
                    return nullptr;
                }
                begin = false;  // 标记已开始处理当前分块
            } while(!parser->isFinshed());  // 循环直到当前分块解析完成
            
            // 调试日志：记录当前分块长度
            SYLAR_LOG_DEBUG(g_logger) << "content_len=" << client_parser.content_len;
            
            // 处理分块数据（+2是为了跳过分块结束标记\r\n）
            if(client_parser.content_len + 2 <= len) {  // 完整分块在缓冲区中
                body.append(data, client_parser.content_len);  // 添加有效数据
                // 移动剩余数据到缓冲区头部（内存重叠安全）
                memmove(data, data + client_parser.content_len + 2, 
                       len - client_parser.content_len - 2);
                len -= client_parser.content_len + 2;  // 更新缓冲区长度
            } else {  // 分块数据不完整
                body.append(data, len);  // 先添加现有数据
                int left = client_parser.content_len - len + 2;  // 计算剩余需要读取的字节
                while(left > 0) {  // 循环读取剩余数据
                    // 计算单次读取大小（不超过缓冲区大小）
                    int rt = read(data, left > (int)buff_size ? (int)buff_size : left);
                    if(rt <= 0) {  // 读取失败
                        close();
                        return nullptr;
                    }
                    body.append(data, rt);  // 追加数据
                    left -= rt;  // 减少剩余字节数
                }
                body.resize(body.size() - 2);  // 移除分块结束标记
                len = 0;  // 缓冲区已空
            }
        } while(!client_parser.chunks_done);  // 循环直到所有分块完成
    } 
    else {  // 固定长度响应体处理
        // 获取Content-Length头指定的body长度
        int64_t length = parser->getContentLength();
        if (length > 0) {  // 存在响应体
            body.resize(length);  // 预分配空间
            
            int len = 0;  // 已拷贝字节数
            if (length >= offset) {  // 缓冲区中有部分body数据
                memcpy(&body[0], data, offset);  // 拷贝缓冲区数据
                len = offset;
            } else {  // 缓冲区数据已超过body长度（异常情况）
                memcpy(&body[0], data, length);
                len = length;
            }
            
            // 读取剩余body数据（如果有）
            length -= offset;
            if (length > 0) {
                if (readFixSize(&body[len], length) <= 0) {  // 精确读取指定长度
                    close();
                    return nullptr;
                }
            }
        }
    }

    // 5. 内容编码处理（如gzip解压）
    if (!body.empty()) {
        auto content_encoding = parser->getData()->getHeader("content-encoding");
        // gzip解压处理
        if (strcasecmp(content_encoding.c_str(), "gzip") == 0) {
            auto zs = ZlibStream::CreateGzip(false);  // 创建解压流
            zs->write(body.c_str(), body.size());    // 写入压缩数据
            zs->flush();                            // 执行解压
            zs->getResult().swap(body);             // 替换为解压后数据
        } 
        // deflate解压处理
        else if (strcasecmp(content_encoding.c_str(), "deflate") == 0) {
            auto zs = ZlibStream::CreateDeflate(false);
            zs->write(body.c_str(), body.size());
            zs->flush();
            zs->getResult().swap(body);
        }
        // 更新响应对象的body数据
        parser->getData()->setBody(body);
    }

    // 6. 返回最终解析结果
    return parser->getData();
}

int HttpConnection::sendRequest(HttpRequest::ptr req) {
    std::stringstream ss;
    ss << *req;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

HttpResult::ptr HttpConnection::DoGet(const std::string& url, uint64_t timeout_ms, 
                                      const std::map<std::string, std::string>& headers,
                                      const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if (!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URI, nullptr, "invalid url" + url);
    }     
    return DoGet(uri, timeout_ms, headers, body);                         
}

HttpResult::ptr HttpConnection::DoGet(Uri::ptr uri, uint64_t timeout_ms, 
                                      const std::map<std::string, std::string>& headers,
                                      const std::string& body) {
    return DoRequest(HttpMethod::GET, uri, timeout_ms, headers, body);                     
}

HttpResult::ptr HttpConnection::DoPost(const std::string& url, uint64_t timeout_ms, 
                                      const std::map<std::string, std::string>& headers,
                                      const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if (!uri) {
    return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URI, nullptr, "invalid url" + url);
    }     
    return DoPost(uri, timeout_ms, headers, body);                         
}

HttpResult::ptr HttpConnection::DoPost(Uri::ptr uri, uint64_t timeout_ms, 
                                     const std::map<std::string, std::string>& headers,
                                     const std::string& body) {
    return DoRequest(HttpMethod::POST, uri, timeout_ms, headers, body);                     
}

HttpResult::ptr HttpConnection::DoRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, 
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& body) {
    Uri::ptr uri = Uri::Create(url);
    if (!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URI, nullptr, "invlaid url: " + url);
    }
    return DoRequest(method, uri, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnection::DoRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, 
                                         const std::map<std::string, std::string>& headers,
                                         const std::string& body) {
    HttpRequest::ptr req = std::make_shared<HttpRequest>();
    req->setPath(uri->getPath());
    req->setQuery(uri->getQuery());
    req->setFragment(uri->getFragment());
    req->setMethod(method);
    bool has_host = false;
    for (auto& i : headers) {
        if (strcasecmp(i.first.c_str(), "connection") == 0) {
            if (strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false); //如果Connection头的值是keep-alive，则保持连接;告诉底层连接不要关闭（启用 HTTP Keep-Alive）
                continue;
            }
        }
        if (!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }
        req->setHeader(i.first, i.second);
    }
    //HTTP协议要求：每个请求必须包含有效的Host头,所以传入的header如果没有host需要手动传入
    if (!has_host) {
        req->setHeader("Host", uri->getHost());
    }
    req->setBody(body);
    return DoRequest(req, uri, timeout_ms);
}

HttpResult::ptr HttpConnection::DoRequest(HttpRequest::ptr req, Uri::ptr uri, uint64_t timeout_ms) {
    bool is_ssl = uri->getScheme() == "https";
    Address::ptr addr = uri->createAddress();
    if (!addr) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_HOST, nullptr, "invalid host: " + uri->getHost());
    }
    Socket::ptr sock = is_ssl ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
    if (!sock) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CREATE_SOCKET_ERROR, nullptr,
                                            "create socket fail " + addr->toString()
                                            + " errno= " + std::to_string(errno)
                                            + " errstr= " + std::string(strerror(errno)));
    }
    //尝试连接
    if (sock->connect(addr)) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CONNECT_FAIL, nullptr, "connect fail: " + addr->toString());
    }
    sock->setRecvTimeout(timeout_ms);
    HttpConnection::ptr conn = std::make_shared<HttpConnection>(sock);
    int rt = conn->sendRequest(req);
    if (rt == 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER
                                            , nullptr, "send request closed by peer: " + addr->toString());
    }
    if (rt < 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_SOCKET_ERROR, nullptr, "send socket error");
    }
    auto rsp = conn->recvResponse();
    if (!rsp) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::TIMEOUT
                                            , nullptr, "recv response timeout: " + addr->toString()
                                            + " timeout_ms:" + std::to_string(timeout_ms));
    }
    return std::make_shared<HttpResult>((int)HttpResult::Error::OK, rsp, "ok");
}

HttpConnectionPool::ptr HttpConnectionPool::Create(const std::string& uri, const std::string& vhost,
                        uint32_t max_size, uint32_t max_alive_time, uint32_t max_request) {
    Uri::ptr turi = Uri::Create(uri);
    if (!turi) {
        SYLAR_LOG_ERROR(g_logger) << "invalid uri= " << turi;
    }
    return std::make_shared<HttpConnectionPool>(turi->getHost(), vhost, turi->getPort(), turi->getScheme() == "https", max_size, max_alive_time, max_request);
}

HttpConnectionPool::HttpConnectionPool(const std::string& host
                                      ,const std::string& vhost
                                      ,uint32_t port
                                      ,bool is_https
                                      ,uint32_t max_size
                                      ,uint32_t max_alive_time
                                      ,uint32_t max_request)
        :m_host(host)
        ,m_vhost(vhost)
        ,m_port(port ? port : (is_https ? 443 : 80))
        ,m_maxSize(max_size)
        ,m_maxAliveTime(max_alive_time)
        ,m_maxRequest(max_request)
        ,m_isHttps(is_https) {
}

HttpConnection::ptr HttpConnectionPool::getConnection() {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<HttpConnection*> invalid_conns;
    HttpConnection* ptr = nullptr;
    MutexType::Lock lock(m_mutex);
    while (!m_conns.empty()) {
        auto conn = *m_conns.begin();
        m_conns.pop_front();
        if (!conn->isConnected()) {
            invalid_conns.push_back(conn);
            continue;
        }
        if (conn->m_createTime + m_maxAliveTime >= now_ms) {
            invalid_conns.push_back(conn);
            continue;
        }
        ptr = conn;
        break;
    }
    lock.unlock();
    m_total -= invalid_conns.size();
    for (auto& i : invalid_conns) {
        delete i;
    }
    if (!ptr) {
        IPAddress::ptr addr = Address::LookupAnyIPAddress(m_host);
        if (!addr) {
            SYLAR_LOG_ERROR(g_logger) << "get addr fail: " << m_host;
            return nullptr;
        }
        addr->setPort(m_port);
        Socket::ptr sock = m_isHttps ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
        if (!sock) {
            SYLAR_LOG_ERROR(g_logger) << "create sock fail: " << *addr;
            return nullptr;
        }
        if (!sock->connect(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "sock connect fail: " << *addr;
            return nullptr;
        }
        ptr = new HttpConnection(sock);
        ++m_total;
    }
    return HttpConnection::ptr(ptr, std::bind(&HttpConnectionPool::ReleasePtr, std::placeholders::_1, this));
}

void HttpConnectionPool::ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool) {
    ++ptr->m_request;
    if (!ptr->isConnected() || (ptr->m_createTime + pool->m_maxAliveTime >= sylar::GetCurrentMS()) || ptr->m_request >= pool->m_maxRequest) {
        delete ptr;
        --pool->m_total;
        return;
    }
    MutexType::Lock lock(pool->m_mutex);
    pool->m_conns.push_back(ptr);
}

HttpResult::ptr HttpConnectionPool::doGet(const std::string& url, uint64_t timeout_ms, 
                                         const std::map<std::string, std::string>& headers = {},
                                         const std::string& body = "") {
    return doRequest(HttpMethod::GET, url, timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doGet(Uri::ptr uri, uint64_t timeout_ms, 
                                          const std::map<std::string, std::string>& headers = {},
                                          const std::string& body = "") {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doGet(ss.str(), timeout_ms, headers, body);                                        
}

HttpResult::ptr HttpConnectionPool::doPost(const std::string& url, uint64_t timeout_ms, 
                                          const std::map<std::string, std::string>& headers = {},
                                          const std::string& body = "") {
    return doRequest(HttpMethod::POST, url, timeout_ms, headers, body);                                        
}

HttpResult::ptr HttpConnectionPool::doPost(Uri::ptr uri, uint64_t timeout_ms, 
                                          const std::map<std::string, std::string>& headers = {},
                                          const std::string& body = "") {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doPost(ss.str(), timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, 
                                             const std::map<std::string, std::string>& headers = {},
                                             const std::string& body = "") {
    std::stringstream ss;
    ss << uri->getPath()
        << (uri->getQuery().empty() ? "" : "?")
        << uri->getQuery()
        << (uri->getFragment().empty() ? "" : "#")
        << uri->getFragment();
    return doRequest(method, ss.str(), timeout_ms, headers, body);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, 
                                             const std::map<std::string, std::string>& headers = {},
                                             const std::string& body = "") {
    HttpRequest::ptr req = std::make_shared<HttpRequest>();
    req->setPath(url);
    req->setMethod(method);
    req->setClose(false);
    bool has_host = false;
    for (auto& i : headers) {
        if (strcasecmp(i.first.c_str(), "connection") == 0) {
            if (strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false);
            }
            continue;
        }
        if (!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }
        req->setHeader(i.first, i.second);
    }
    if (!has_host) {
        if (m_vhost.empty()) {
            req->setHeader("Host", m_host);
        } else {
            req->setHeader("Host", m_vhost);
        }
    }
    req->setBody(body);
    return doRequest(req, timeout_ms);
}

HttpResult::ptr HttpConnectionPool::doRequest(HttpRequest::ptr req, uint64_t timeout_ms) {
    auto conn = getConnection();
    if (!conn) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::POOL_GET_CONNECTION,
                                            nullptr, "pool hast:" + m_host + " port:" + std::to_string(m_port));
    }
    auto sock = conn->getSocket();
    if (!sock) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::POOL_INVALID_CONNECTION,
                                             nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    sock->setRecvTimeout(timeout_ms);
    int rt = conn->sendRequest(req);
    if (rt == 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER, 
                                             nullptr, "send request closed by peer: " + sock->getRemoteAddress()->toString());
    }
    if(rt < 0) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_SOCKET_ERROR
                                            , nullptr, "send request socket error errno=" + std::to_string(errno)
                                            + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvResponse();
    if (!rsp) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::TIMEOUT
                                            , nullptr, "recv response timeout: " + sock->getRemoteAddress()->toString()
                                            + " timeout_ms:" + std::to_string(timeout_ms));   
    }
    return std::make_shared<HttpResult>((int)HttpResult::Error::OK, rsp, "ok");
}

}
}