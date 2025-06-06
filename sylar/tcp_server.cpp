#include <vector>

#include "tcp_server.h"
#include "config.h"
#include "log.h"

namespace sylar {

static sylar::ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout = 
        sylar::Config::Lookup("tcp_server.read_tieout", (uint64_t)(60 * 1000 * 2),
                                "tcp server read timeout");
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

TcpServer::TcpServer(sylar::IOManager* worker,
                     sylar::IOManager* io_worker,
                     sylar::IOManager* accept_worker) 
    : m_worker(worker),
      m_ioWorker(io_worker),
      m_acceptWorker(accept_worker),
      m_recvTimeout(g_tcp_server_read_timeout->getValue()),
      m_name("sylar.1.0.0"),
      m_isStop(true){
}

TcpServer::~TcpServer() {
    if (!m_socks.empty()) {
        for (auto& i : m_socks) {
            i->close();
        }
    }
    m_socks.clear();
}

void TcpServer::setConf(const TcpServerConf& v) {
    m_conf.reset(new TcpServerConf(v));
}

bool TcpServer::bind(sylar::Address::ptr addr, bool ssl) {
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails, ssl);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs,
                        const std::vector<Address::ptr>& fails,
                        bool ssl = false) {
    m_ssl = ssl;
    for (auto& addr : addrs) {
        Socket::ptr sock = ssl ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
        if(!sock->bind(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "bind fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        if(!sock->listen()) {
            SYLAR_LOG_ERROR(g_logger) << "listen fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        m_socks.push_back(sock);
    }
    if (!fails.empty()) {
        m_socks.clear();
        return false;
    }
    for(auto& i : m_socks) {
        SYLAR_LOG_INFO(g_logger) << "type=" << m_type
            << " name=" << m_name
            << " ssl=" << m_ssl
            << " server bind success: " << *i;
    }
    return true;
}

void TcpServer::startAccept(Socket::ptr sock) {
    while (!m_isStop) {
        Socket::ptr client = sock->accept();
        if (client) {
            client->setRecvTimeout(m_recvTimeout);
            m_ioWorker->schedule(std::bind(&TcpServer::handleClient, shared_from_this(), client));
        } else {
            SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno
                << " errstr=" << strerror(errno);
        }
    }
}

bool TcpServer::start() {
    if (!m_isStop) {
        return true;
    }
    m_isStop = false;
    for (auto& sock : m_socks) {
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept, shared_from_this(), sock));
    }
    return true;
}







void TcpServer::stop() {
    m_isStop = true;
    //获取当前对象的shared_ptr，让当前对象引用计数+1，
    //防止在执行stop时，其他线程已经将该对象析构掉
    //如果被提前析构，会导致下面的lambda执行错误，因为内部访问了当前对象
    auto self = shared_from_this();
    //这里访问m_socks实际是通过this指针，而self的存在保证了this指针始终有效
    m_acceptWorker->schedule([this, self]() {
        for (auto& sock : m_socks) {
            sock->cancelAll();
            sock->close();
        }
        m_socks.clear();
    });
}

void TcpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger)  << "handle client;" << *client;
}

bool TcpServer::loadCertificates(const std::string& cert_file, const std::string& key_file) {
    for (auto& i : m_socks) {
        auto ssl_sock = std::dynamic_pointer_cast<SSLSocket>(i);
        if (ssl_sock) {
            if (!ssl_sock->loadCertificates(cert_file, key_file)) {
                return false;
            }
        }
    }
    return true;
}

std::string TcpServer::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name << " ssl=" << m_ssl
       << " worker=" << (m_worker ? m_worker->getName() : "")
       << " accept=" << (m_acceptWorker ? m_acceptWorker->getName() : "")
       << " recv_timeout=" << m_recvTimeout << "]" << std::endl;
    std::string pfx = prefix.empty() ? "    " : prefix;
    for(auto& i : m_socks) {
        ss << pfx << pfx << *i << std::endl;
    }
    return ss.str();
}


}

