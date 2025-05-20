#include "log.h"
#include "hook.h"
#include "macro.h"
#include "socket.h"
#include "iomanager.h"
#include "fd_manager.h"

#include <netinet/tcp.h>
#include <limits.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");


Socket::ptr Socket::CreateTCP(sylar::Address::ptr address) {
    Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDP(sylar::Address::ptr address) {
    Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket() {
    Socket::ptr sock(new Socket(IPv4, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket() {
    Socket::ptr sock(new Socket(IPv4, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket6() {
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket6() {
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::Socket(int family, int type, int protocol) 
    : m_sock(-1),
      m_family(family),
      m_type(type),
      m_protocol(protocol),
      m_isConnected(false) {
}

Socket::~Socket(){
    close();
}

int64_t Socket::getSendTimeout() {
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) {
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Socket::setSendTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout() {
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) {
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}

void Socket::setRecvTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 / 1000)};
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

//系统调用getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen)
//sockfd：是一个有效的套接字描述符，由socket()函数返回
//level：协议层级，告诉要获取选项的协议层，他告诉getsockopt调用是在应用层、传输层还是网络层
    //SOL_SOCKET：套接字层，表示对通用的套接字选项进行操作，是最常用的层级，适用于与协议无关的选项
    //IPPROTO_IP：IP层，表示与IPV4相关的选项(如TLL，路由选项等)
    //IPPROTO_TCP：TCP层，表示与TCP相关的选项
    //IPPROTO_UDP：UDP层，表示与UDP相关的选项
    //IPPROTO_IPv6：IPv6层，表示与IPv6相关的选项
//optname：选项的名称，指定要获取的具体选项
    //SOL_SOCKET层相关选项
      //SO_RCVBUF：接收缓冲区大小
      //SO_SNDBUF: 发送缓冲区大小
      //SO_SNDTIME0: 发送超时事件
      //SO_RCVTIMEO: 接收超时时间
//optval：指向存储选项值的缓冲区
//optlen：表示缓冲区的大小，getsockopt会在成功时更新它为实际存储的选项值的大小

bool Socket::getOption(int level, int option, void* result, socklen_t* len) {
    int rt = getsockopt(m_sock, level, option, result, (socklen_t*)len);
    if (rt) {
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void* result, socklen_t len) {
    int rt = setsockopt(m_sock, level, option, result, (socklen_t)len);
}

//accept()用于接受连接请求,accept会返回一个新的套接字connect_fd用于与客户端进行通信
Socket::ptr Socket::accept() {
    Socket::ptr sock(new Socket(m_family, m_type, m_protocol));
    //m_sock是一个监听套接字，通过bind和listen函数设置的
    int newsock = ::accept(m_sock, nullptr, nullptr);
    //连接失败返回-1
    if (newsock == -1) {
        return nullptr;
    }
    if (sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

//创建新套接字
void Socket::newSock() {
    m_sock = socket(m_family, m_type, m_protocol);
    if (SYLAR_LIKELY(m_sock != -1)) {
        initSock();
    }
}

//初始化一个socket
void Socket::initSock() {
    int val = 1;
    // SO_REUSEADDR 是一个选项，表示允许重用本地地址（IP地址）。
    // 这通常用于使得一个地址可以在多个套接字之间共享，
    // 避免因程序崩溃或重启时端口仍被占用而导致无法绑定套接字。
    // 特别是在服务器程序中，SO_REUSEADDR 常用于在重启服务时快速重用端口。
    setOption(SOL_SOCKET, SO_REUSEADDR, val);
    if (m_type == SOCK_STREAM) {
        // TCP_NODELAY 选项用于禁用 Nagle 算法。Nagle 算法的目的是减少
        // 小数据包的数量，通常将多个小的数据包合并成一个大包一起发送。
        // TCP_NODELAY 的作用是禁用 Nagle 算法，确保每次发送的数据都尽可能地立刻发送，
        // 而不是等待多个小包合并。启用 TCP_NODELAY 可以减少延迟，
        // 特别是对于一些对实时性要求较高的应用（如在线游戏、语音通信等）。
        setOption(IPPROTO_TCP, TCP_NODELAY, val);
    }
}

//用一个套接字描述符初始化一个socket
bool Socket::init(int sock) {
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sock);
    if (ctx && ctx->isSocket() && !ctx->isClose()) {
        m_sock = sock;
        m_isConnected = true;
        initSock();
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

//send函数只有在套接字处于连接状态时才进行发送：这里的发送指的是通过m_sock发送数据到它所连接的另一端
//返回值为发送的字节数或错误码
//参数flags：标志
   //0：标准发送模式
   //MSG_DONTWAIT：非阻塞模式，立即返回，不会等待数据传输
   //MSG_NOSIGNAL：避免SIGPIPE信号，适用于linux/unix系统
   int Socket::send(const void* buffer, size_t length, int flags) {
	if (isConnected()) {
		return ::send(m_sock, buffer, length, flags);
	}
	return -1;
}

//struct msghdr {
//	   void* msg_name;       // 目的地址（仅用于 UDP）
//	   socklen_t     msg_namelen;    // 目的地址的大小
//	   struct iovec* msg_iov;        // 指向数据缓冲区数组
//	   size_t        msg_iovlen;     // 数据缓冲区数组的大小
//	   void* msg_control;    // 指向辅助数据（如控制信息）
//	   size_t        msg_controllen; // 控制数据的大小
//	   int           msg_flags;      // 处理标志
// };
int Socket::send(const iovec* buffers, size_t length, int flags) {
	if (isConnected()) {
		msghdr msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = (iovec*)buffers;
		msg.msg_iovlen = length;
		return ::sendmsg(m_sock, &msg, flags);
	}
	return -1;
}

int Socket::sendTo(const void* buffer, size_t lenght, const Address::ptr to, int flags) {
	if (isConnected()) {
		return ::sendto(m_sock, buffer, lenght, flags, to->getAddr(), to->getAddrLen());
	}
	return -1;
}

int Socket::sendTo(const iovec* buffers, size_t length, const Address::ptr ot, int flags) {
	if (isConnected()) {
		msghdr msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = (iovec*)buffers;
		msg.msg_iovlen = length;
		return ::sendmsg(m_sock, &msg, flags);
	}
}

//让m_sock 从已连接的对端套接字接收数据
int Socket::recv(void* buffer, size_t length, int flags) {
	if (isConnected()) {
		return ::recv(m_sock, buffer, length, flags);
	}
	return -1;
}

int Socket::recv(iovec* buffer, size_t length, int flags) {
	if (isConnected()) {
		msghdr msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = (iovec*)buffer;
		msg.msg_iovlen = length;
		return ::recvmsg(m_sock, &msg, flags);
	}
	return -1;
}

int Socket::recvFrom(void* buffer, size_t length, Address::ptr from, int flags) {
	if (isConnected()) {
		socklen_t len = from->getAddrLen();
		return ::recvfrom(m_sock, buffer, length, flags, from->getAddr(), &len);
	}
	return -1;
}

int Socket::recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags) {
	if (isConnected()) {
		msghdr msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = (iovec*)buffers;
		msg.msg_iovlen = length;
		msg.msg_name = from->getAddr();
		msg.msg_namelen = from->getAddrLen();
		return ::recvmsg(m_sock, &msg, flags);
	}
	return -1;
}

bool Socket::isValid() const {
	return m_sock != -1;
}

//获取套接字的最新错误状态
int Socket::getError() {
	int error = 0;
	socklen_t len = sizeof(error);
	if (!getOption(SOL_SOCKET, SO_ERROR, &error, &len)) {
		//如果getsockopt调用失败会设置errno
		error = errno;
	}
	return error;
}

std::ostream& Socket::dump(std::ostream& os) const {
	os << "sock: " << m_sock
	   << "is_connected:" << m_isConnected
	   << " family=" << m_family
	   << " type=" << m_type
       << " protocol=" << m_protocol;
	if (m_localAddress) {
		os << "local_address: " << m_localAddress->toString();
	}
	if (m_remoteAddress) {
		os << "remote_address: " << m_remoteAddress->toString();
	}
	os << ".";
	return os;
}

std::string Socket::toString() const {
	std::stringstream ss;
	dump(ss);
	return ss.str();
}


//获取远程地址(对端地址)，即当前套接字连接的另一端地址，主要用于TCP连接，调用getpeername来获取对端sockaddr结构体，并转换为Address::ptr
Address::ptr Socket::getRemoteAddress() {
	Address::ptr result;
	switch (m_family) {
		case AF_INET:
			result.reset(new IPv4Address());
			break;
		case AF_INET6:
			result.reset(new IPv6Address());
			break;
		case AF_UNIX:
			result.reset(new UnixAddress());
			break;
		default:
			result.reset(new UnknownAddress(m_family));
			break;
	}
	//getpeername获取远程当前套接字连接的远端地址信息，存入result->getAddr中
	socklen_t addrlen = result->getAddrLen();
	if (getpeername(m_sock, result->getAddr(), &addrlen)) {
		//获取失败
		return Address::ptr(new UnknownAddress(m_family));
	}
	if (m_family == AF_UNIX) {
		//需要手动设置UnixAddress的地址长度，否则sockaddr_un结构体的sun_path可能会被截断
		UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
		addr->setAddrLen(addrlen);
	}
	m_remoteAddress = result;
	return m_remoteAddress;
}

//获取当前socket绑定的本质地址，通过getsockname获取本地地址(绑定的ip和端口)
Address::ptr Socket::getLocalAddress() {
	Address::ptr result;
	switch (m_family) {
		case AF_INET:
			result.reset(new IPv4Address());
			break;
		case AF_INET6:
			result.reset(new IPv6Address());
			break;
		case AF_UNIX:
			result.reset(new UnixAddress());
			break;
		default:
			result.reset(new UnknownAddress(m_family));
			break;
	}
	socklen_t addrlen = result->getAddrLen();
	if (getsockname(m_sock, result->getAddr(), &addrlen)) {
		return Address::ptr(new UnknownAddress(m_family));
	}
	if (m_family == AF_UNIX) {
		UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
		addr->setAddrLen(addrlen);
	}
	m_localAddress = result;
	return m_localAddress;
}


//recv相关函数对应READ事件：recv本质是等待数据可读，如果socket还没有数据可读，
//就要等待READ事件发生，当READ事件发生时，IOManager会通知recv继续读取数据

//recv操作的本质是从socket的接收缓冲区读取数据，即获取对端发来的数据
//当 socket 的接收缓冲区 有数据可读 时，操作系统就会触发 READ 事件，
//通知 IOManager 这个 socket 现在可以读取数据了。
//如果缓冲区没数据，recv() 会阻塞或返回 EAGAIN，直到 READ 事件发生，IOManager 重新调度 recv()。

//取消m_sock监听的读事件
bool Socket::cancelRead() {
	return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

//send操作的本质是把数据写入到socket的发送缓冲区，然后让操作系统负责把数据发送到连接的对端
//当发送缓冲区有空间存放新数据时，操作系统就会触发WRITE事件，如果缓冲区满了，
// send会阻塞或返回EAGAIN，直到WRITE事件发生，IOManager重新调度send

//send相关函数对应WRITE事件：send操作本质是等待socket可写，如果socket缓冲区满了，
//send可能会阻塞，直到WRITE事件发生，WRITE事件发生时说明socket可写，IOManager会通知send继续写入数据

//取消m_sock监听的写事件
bool Socket::cancelWrite() {
	return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::WRITE);
}

//取消m_sock的accept()监听(本质是READ事件)
bool Socket::cancelAccept() {
	return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

//取消m_sock上所有的io事件
bool Socket::cancelAll() {
	return IOManager::GetThis()->cancelAll(m_sock);
}


//绑定本地IP地址和端口
bool Socket::bind(const Address::ptr addr) {
	if (!isValid()) {
		newSock();
		if (SYLAR_UNLIKELY(!isValid())) {
			return false;
		}
	}
	//检查地址族是否匹配
	if (SYLAR_UNLIKELY(addr->getFamily() != m_family)) {
		std::cout << "地址族不匹配" << std::endl;
		return false;
	}

	//处理unix域套接字
	//unix域套接字不像tcp套接字那样绑定到IP地址和端口号，而是绑定到一个文件路径
	//调用bind时，bind不是在已有路径上绑定，而是创建新路径
	//如果路径已经存在，表明该地址已被使用(这个路径已经有别的套接字绑定了)，此时会绑定失败
	//如果路径不存在，则绑定成功，内核在这个路径创建socket文件
	UnixAddress::ptr uaddr = std::dynamic_pointer_cast<UnixAddress>(addr);
	//如果addr不是Unix类型地址会返回一个nullptr
	if (uaddr) {
		Socket::ptr sock = Socket::CreateUnixTCPSocket();
		if (sock->connect(uaddr)) {
			//如果连接成功，说明socket已被占用，返回失败
			return false;
		}
		else {
			//删除该路径，确保bind不会因路径存在而失败
			sylar::FSUtil::Unlink(uaddr->getPath(), true);   
		}
	}

	if (::bind(m_sock, addr->getAddr(), addr->getAddrLen())) {
		SYLAR_LOG_ERROR(g_logger) << errno << strerror(errno);
		return false;
	}

	//在bind()之后会为本地地址分配ip和端口号
	getLocalAddress();

	return true;
}

bool Socket::reconnect(uint64_t timeout_ms) {
	//首先检查远端路径是否为空
	if (!m_remoteAddress) {
		return false;  //远端为空
	}
	//新连接可能会使用新的本地ip和端口
	//如果不清除，可能会错误的复用旧的本地地址，导致connect()失败
	m_localAddress.reset();
	return connect(m_remoteAddress, timeout_ms);
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms) {
	m_remoteAddress = addr;
	if (!isValid()) {
		newSock();
		if (SYLAR_UNLIKELY(!isValid())) {
			return false;
		}
	}
	if (SYLAR_UNLIKELY(addr->getFamily() != m_family)) {
		return false;
	}
	//无超时
	if (timeout_ms == (uint64_t)-1) {
		if (::connect(m_sock, addr->getAddr(), addr->getAddrLen())) {
			//如果连接失败，记录错误代码errno，close()关闭socket
			close();
			return false;
		}
	}
	else {   //有超时
		if (::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(), timeout_ms)) {
			close();
			return false;
		}
	}
	m_isConnected = true;
	getRemoteAddress();
	getLocalAddress();
	return true;
}

//backlog它不是最大的并发连接数而是未处理连接队列的长度，listen(sockfd, 5)代表最多5各未accept的来连接可以排队
//将一个socket变成监听socket，使其可以接受新的连接请求，一般用于服务器端，
//在调用bind绑定端口之后，调用listen进入监听状态
bool Socket::listen(int backlog) {
	if (!isValid()) {
		return false;
	}
	if (::listen(m_sock, backlog)) {
		return false;
	}
	return true;
}

bool Socket::close() {
	if (!m_isConnected && m_sock == -1) {
		return true;
	}
	m_isConnected = false;
	if (m_sock != -1) {
		::close(m_sock);
		m_sock = -1;
	}
	return false;
}
    
//匿名命名空间：
//匿名命名空间的作用域仅限于当前的文件。任何在匿名命名空间中声明的变量、类、函数等，只能在该源文件中使用，外部无法访问。这种做法通常用于隐藏内部实现细节，避免命名冲突。
namespace {

//该结构体的作用是在程序启动时初始化OpenSSL库
struct _SSLInit {
    _SSLInit() {
        //初始化openssl库，设置ssl环境
        SSL_library_init();
        //加载openssl错误字符串
        SSL_load_error_strings();
        //加载OpenSSL_add_all_algorithms()，加载OpenSSL的所有加密算法
        OpenSSL_add_all_algorithms();
    }
};
//静态变量，会在程序启动时创建一次，并且生命周期是程序运行的整个过程
static _SSLInit s_init;

}

SSLSocket::SSLSocket(int family, int type, int protocol) :
    Socket(family, type, protocol) {
}

std::ostream& SSLSocket::dump(std::ostream& os) const {
    os << "[SSLSocket sock=" << m_sock
        << " is_connected=" << m_isConnected
        << " family=" << m_family
        << " type=" << m_type
        << " protocol=" << m_protocol;
    if (m_localAddress) {
        os << "local_address=" << m_localAddress->toString();
    }
    if (m_remoteAddress) {
        os << "remote_address=" << m_remoteAddress->toString();
    }
    os << "]";
    return os;
}

SSLSocket::ptr SSLSocket::CreateTCP(sylar::Address::ptr address) {
    SSLSocket::ptr sock(new SSLSocket(address->getFamily(), TCP, 0));
    return sock;
}

SSLSocket::ptr SSLSocket::CreateTCPSocket() {
    SSLSocket::ptr sock(new SSLSocket(IPv4, TCP, 0));
    return sock;
}

SSLSocket::ptr SSLSocket::CreateTCPSocket6() {
    SSLSocket::ptr sock(new SSLSocket(IPv6, TCP, 0));
    return sock;
}

bool SSLSocket::loadCertificates(const std::string& cert_file, const std::string& key_file) {
    //创建一个新的SSL上下文对象
    //SSLv23_server_method会创建一个向后兼容的TLS/SSL服务端方法
    m_ctx.reset(SSL_CTX_new(SSLv23_server_method()), SSL_CTX_free);
    //加载证书链文件
    if (SSL_CTX_use_certificate_chain_file(m_ctx.get(), cert_file.c_str()) != 1) {
        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_certificate_chain_file(" << cert_file << ")error";
        return false;
    }
    //加载私钥文件
    if (SSL_CTX_use_PrivateKey_file(m_ctx.get(), key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        //ssl_filetype_pem表示文件是pem格式(纯文本)
        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_Privatekey_file(" << key_file << ")error";
        return false;
    }
    //检查加载的证书和私钥是否匹配
    if (SSL_CTX_check_private_key(m_ctx.get()) != 1) {
        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_check_private_key cert_file=" << cert_file << " key_file=" << key_file;
        return false;
    }
    return true;
}

//初始化一个SSL套接字，将给定的文件描述符转为支持SSL的连接，并完成握手SSL_accept
//该函通常在服务端接收到客户端连接之后被调用，属于服务端SSL套接字的初始化逻辑
bool SSLSocket::init(int sock) {
    //sock来自accept的返回值，即服务端中与客户端连接的fd
    bool v = Socket::init(sock);
    if (v) {
        //创建一个新的ssl对象，表示一次具体的ssl会话
        m_ssl.reset(SSL_new(m_ctx.get()), SSL_free);
        //将当前的socket文件描述符sock绑定到这个SSL会话上,告诉openssl，加密通信要通过这个fd来走
        SSL_set_fd(m_ssl.get(), m_sock);
        //进行ssl握手
        //服务端首先调用SSL_accept()但是他会阻塞等待客户端发起SSL握手
        //客户端后调用SSL_connect()它主动发起握手流程，驱动整个握手开始
        v = (SSL_accept(m_ssl.get()) == 1);
    }
    return v;
}

bool SSLSocket::bind(const Address::ptr addr) {
    return Socket::bind(addr);
}

bool SSLSocket::listen(int backlog) {
    return Socket::listen(backlog);
}

Socket::ptr SSLSocket::accept() {
    SSLSocket::ptr sock(new SSLSocket(m_family, m_type, m_protocol));
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if (newsock == -1) {
        return nullptr;
    }
    sock->m_ctx = m_ctx;
    if (sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

//连接一个远程地址，并在连接成功后，升级为SSL加密通信
bool SSLSocket::connect(const Address::ptr addr, uint64_t timeout_ms) {
    bool v = Socket::connect(addr, timeout_ms);
    if (v) {
        //连接成功后进行SSL初始化
        //创建一个新的客户端SSL上下文
        m_ctx.reset(SSL_CTX_new(SSLv23_client_method()), SSL_CTX_free);
        m_ssl.reset(SSL_new(m_ctx.get()), SSL_free);
        SSL_set_fd(m_ssl.get(), m_sock);
        //发送客户端问候消息，触发TLS握手
        v = (SSL_connect(m_ssl.get()) == 1);
    }
    return v;
}

bool SSLSocket::close() {
    return Socket::close();
}

//send
int SSLSocket::send(const void* buffer, size_t length, int flags) {
    //m_ssl需要有效，即需要先调用connect或accept后才有m_ssl
    if (m_ssl) {
        return SSL_write(m_ssl.get(), buffer, length);
    }
    return -1;
}

int SSLSocket::send(const iovec* buffers, size_t length, int flags) {
    if (!m_ssl) return -1;
    int total = 0;
    for (int i = 0; i < length; ++i) {
        int tmp = SSL_write(m_ssl.get(), buffers[i].iov_base, buffers[i].iov_len);
        //发送失败或连接中断，立即返回
        if (tmp <= 0) {
            return tmp;
        }
        total += tmp;
        //如果实际发送的字节数不等于当前buffer的长度，就中断循环，因为这种情况说明socket写缓冲区已经满了，不能再继续发送了
        if (tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

//SSL是基于TCP的，只能面向连接通信，而sendTo是用于UDP(无连接通信的)，所以直接将sendTo写死
int SSLSocket::sendTo(const void* buffer, size_t length, const Address::ptr to, int flags) {
    SYLAR_ASSERT(false);
    return -1;
}

int SSLSocket::sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags) {
    SYLAR_ASSERT(false);
    return -1;
}



//recv
int SSLSocket::recv(void* buffer, size_t length, int flags) {
    if (m_ssl) {
        return SSL_read(m_ssl.get(), buffer, length);
    }
    return -1;
}

int SSLSocket::recv(iovec* buffers, size_t length, int flags) {
    if (!m_ssl) return -1;
    int total = 0;
    for (size_t i = 0; i < length; ++i) {
        int tmp = SSL_read(m_ssl.get(), buffers[i].iov_base, buffers[i].iov_len);
        if (tmp <= 0) return tmp;
        total += tmp;
        if (tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

int SSLSocket::recvFrom(void* buffer, size_t length, Address::ptr from, int flags) {
    return -1;
}

int SSLSocket::recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags) {
    return -1;
}

std::ostream& operator<<(std::ostream& os, const Socket& sock) {
    return sock.dump(os);
}


}
