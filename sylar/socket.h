/**
 * @file socket.h
 * @brief socket封装
 * @date 2025-04-02
 * @copyright Copyright (c) 2025年 All rights reserved
 */

#ifndef __SYLAR_SOCKET_H__
#define __SYLAR_SOCKET_H__

#include "address.h"
#include "noncopyable.h"

#include <memory>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <netinet/tcp.h>

namespace sylar
{

// Socket 类封装了底层 Socket API，提供 TCP、UDP、IPv4、IPv6、Unix 等多种类型的创建、
// 连接、绑定、监听、发送、接收等操作，支持超时控制、错误处理和 RAII 资源管理，
// 并结合协程调度器，简化网络编程，提高代码的可读性和健壮性。
class Socket : public std::enable_shared_from_this<Socket>, Noncopyable
{
public:
    typedef std::shared_ptr<Socket> ptr;
    typedef std::weak_ptr<Socket> weak_ptr;

    // socket类型
    enum Type
    {
        TCP = SOCK_STREAM,
        UDP = SOCK_DGRAM
    };

    // 协议族
    enum Family
    {
        IPv4 = AF_INET,
        IPv6 = AF_INET6,
        UNIX = AF_UNIX
    };

    // 静态函数

    // 根据传入的address，创建一个TCP Socket
    // address可以是IPv4 Ipv6 Unix地址
    // 适用于需要动态决定socket协议族的情况
    static Socket::ptr CreateTCP(sylar::Address::ptr address);

    // 根据传入的address创建一个UDP socket
    static Socket::ptr CreateUDP(sylar::Address::ptr address);

    // 创建一个IPv4的TCP Socket(AF_INET+SOCK_STREAM)
    static Socket::ptr CreateTCPSocket();

    // 创建一个IPv4的UDP Socket(AF_INET+SOCK_DGRAM)
    static Socket::ptr CreateUDPSocket();

    // 创建一个IPv6的TCP Socket(AF_INET6+SOCK_STREAM)
    static Socket::ptr CreateTCPSocket6();

    // 创建一个IPv6的UDP Socket(AF_INET6+SOCK_DGRAM)
    static Socket::ptr CreateUDPSocket6();

    // 创建一个Unix域的TCP Socket(AF_UNIX+SOCK_STREAM)
    static Socket::ptr CreateUnixTCPSocket();

    // 创建一个Unix域的UDP Socket(AF_UNIX+SOCK_DGRAM)
    static Socket::ptr CreateUnixUDPSocket();

    // family：协议族
    // type：类型
    // protocol：协议
    Socket(int family, int type, int protocol = 0);

    virtual ~Socket();

    // 获取当前socket的发送超时时间 SO_SNDTIMEO
    int64_t getSendTimeout();
    // 设置当前socket的发送超时时间
    void setSendTimeout(int64_t v);

    // 获取当前socket的接收超时时间 SO_RCVTIMEO
    int64_t getRecvTimeout();
    // 设置当前socket的接收超时时间
    void setRecvTimeout(int64_t v);

    // 获取socket的某个选项值，相当于getsockopt()的封装
    // level：指定选项所属的协议层
    // option：执行选项类型
    // result：获取到的值
    // len：记录获取到的数据的长度
    bool getOption(int level, int option, void *result, socklen_t *len);

    template <class T>
    bool getOption(int level, int option, T &result)
    {
        socklen_t length = sizeof(T);
        return getOption(level, option, &result, &length);
    }

    // 设置socket的某个选项值
    bool setOption(int level, int option, const void *result, socklen_t len);
    template <class T>
    bool setOption(int level, int option, T &value)
    {
        return setOption(level, option, &value, sizeof(T));
    }

    /**
     * @brief 接收connect链接
     * @return 成功返回新连接的socket,失败返回nullptr
     * @pre Socket必须 bind , listen  成功
     */
    virtual Socket::ptr accept();

    /**
     * @brief 绑定地址
     * @param[in] addr 地址
     * @return 是否绑定成功
     */
    virtual bool bind(const Address::ptr addr);

    /**
     * @brief 连接地址
     * @param[in] addr 目标地址
     * @param[in] timeout_ms 超时时间(毫秒)
     */
    virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1);

    virtual bool reconnect(uint64_t timeout_ms = -1);

    /**
     * @brief 监听socket
     * @param[in] backlog 未完成连接队列的最大长度
     * @result 返回监听是否成功
     * @pre 必须先 bind 成功
     */
    virtual bool listen(int backlog = SOMAXCONN);

    /**
     * @brief 关闭socket
     */
    virtual bool close();

    /**
     * @brief 发送数据
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int send(const void *buffer, size_t length, int flags = 0);

    /**
     * @brief 发送数据
     * @param[in] buffers 待发送数据的内存(iovec数组)
     * @param[in] length 待发送数据的长度(iovec长度)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int send(const iovec *buffers, size_t length, int flags = 0);

    /**
     * @brief 发送数据
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int sendTo(const void *buffer, size_t length, const Address::ptr to, int flags = 0);

    /**
     * @brief 发送数据
     * @param[in] buffers 待发送数据的内存(iovec数组)
     * @param[in] length 待发送数据的长度(iovec长度)
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int sendTo(const iovec *buffers, size_t length, const Address::ptr to, int flags = 0);

    /**
     * @brief 接受数据
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recv(void *buffer, size_t length, int flags = 0);

    /**
     * @brief 接受数据
     * @param[out] buffers 接收数据的内存(iovec数组)
     * @param[in] length 接收数据的内存大小(iovec数组长度)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recv(iovec *buffers, size_t length, int flags = 0);

    /**
     * @brief 接受数据
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[out] from 发送端地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recvFrom(void *buffer, size_t length, Address::ptr from, int flags = 0);

    /**
     * @brief 接受数据
     * @param[out] buffers 接收数据的内存(iovec数组)
     * @param[in] length 接收数据的内存大小(iovec数组长度)
     * @param[out] from 发送端地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recvFrom(iovec *buffers, size_t length, Address::ptr from, int flags = 0);

    /**
     * @brief 获取远端地址
     */
    Address::ptr getRemoteAddress();

    /**
     * @brief 获取本地地址
     */
    Address::ptr getLocalAddress();

    /**
     * @brief 获取协议簇
     */
    int getFamily() const { return m_family; }

    /**
     * @brief 获取类型
     */
    int getType() const { return m_type; }

    /**
     * @brief 获取协议
     */
    int getProtocol() const { return m_protocol; }

    /**
     * @brief 返回是否连接
     */
    bool isConnected() const { return m_isConnected; }

    /**
     * @brief 是否有效(m_sock != -1)
     */
    bool isValid() const;

    /**
     * @brief 返回Socket错误
     */
    int getError();

    /**
     * @brief 输出信息到流中
     */
    virtual std::ostream &dump(std::ostream &os) const;

    virtual std::string toString() const;

    /**
     * @brief 返回socket句柄
     */
    int getSocket() const { return m_sock; }

    /**
     * @brief 取消读
     */
    bool cancelRead();

    /**
     * @brief 取消写
     */
    bool cancelWrite();

    /**
     * @brief 取消accept
     */
    bool cancelAccept();

    /**
     * @brief 取消所有事件
     */
    bool cancelAll();

protected:
    // 初始化套接字相关资源
    void initSock();
    // 创建新的套接字
    void newSock();
    // 初始化已有的套接字
    virtual bool init(int sock);

protected:
    int m_sock;                   // socket句柄
    int m_family;                 // 协议族
    int m_type;                   // 类型
    int m_protocol;               // 协议
    bool m_isConnected;           // 是否已连接
    Address::ptr m_localAddress;  // 本机地址
    Address::ptr m_remoteAddress; // 远端地址
};

// 在网络通信中，我们常用的socket实现是基于TCP/IP协议的，但是，TCP是明文传输的，不安全的
// 明文传输，发送的数据没有经过任何加密处理，直接以原始形式在网络上传输，换句话说，只要能监听到这段数据包，就能直接看到里面的内容
// 假设你用普通的 TCP socket（比如 C++ 的 send() / recv()）写了一个聊天程序，客户端发送一段消息：
// Hello, how are you ?
// TCP 会把这句话原封不动地打包成数据报文，在网络中发送出去。任何在网络路径中的人（比如路由器、运营商、黑客），如果有能力抓包（比如用 Wireshark），就能直接看到这一句完整的聊天内容。
// 就像这样：
//[数据包内容]：
// 48 65 6C 6C 6F 2C 20 68 6F 77 20 61 72 65 20 79 6F 75 3F
//= > Hello, how are you ?
// 如果使用普通的socket，数据会直接暴露在网络中间环节，很容易被中间人嗅探，篡改、伪造，这就是所谓的中间人攻击。或被动监听
// 为了安全传输，我们需要对socket传输的数据进行加密、认证、完整性校验，这就是SSL/TSL的用途，也就是SSLSocket的意义

// 时间 →
// 客户端                              服务端
// ┌──────────────┐                ┌────────────────┐
// │ 创建 socket   │                │ 创建监听 socket │
// │              │                │ bind()         │
// │              │                │ listen()       │
// │              │                │                │
// │              │                │ accept() ← 阻塞等待连接
// │ connect() → 发起 TCP三次握手  │                │
// │              │                │ ← 接收到握手请求
// │              │                │ 三次握手完成 → accept() 返回新连接fd
// │              │                │                │
// │ 创建 SSL_CTX │                │ 创建 SSL_CTX    │（通常服务端程序启动时做）
// │ 创建 SSL     │                │ 创建 SSL        │（为刚返回的 fd）
// │ SSL_set_fd   │                │ SSL_set_fd      │
// │ SSL_connect()│ → 发起 SSL握手 │                │
// │              │                │ ← SSL_accept() 开始处理握手
// │              │                │ 发送证书、协商参数...
// │ 校验证书     │                │                │
// │ 发送密钥信息 │                │                │
// │ 完成握手     │ ← 返回成功(1)   │ ← 返回成功(1)    │
// │              │                │                │
// │ —— 建立加密通道 ——           │ —— 建立加密通道 —— │
// │ send() / recv() 开始通信       │ send() / recv() 开始通信
// └──────────────┘                └────────────────┘

class SSLSocket : public Socket {
public:
    typedef std::shared_ptr<SSLSocket> ptr;

    // 创建一个SSL套接字对象
    SSLSocket(int family, int type, int protocol = 0);

    // 根据地址类型创建对应的SSL套接字
    static SSLSocket::ptr CreateTCP(sylar::Address::ptr address);
    static SSLSocket::ptr CreateTCPSocket();
    static SSLSocket::ptr CreateTCPSocket6();

    virtual bool bind(const Address::ptr addr) override;
    virtual bool listen(int backlog = SOMAXCONN) override;
    virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1) override;
    virtual Socket::ptr accept() override;
    virtual bool close() override;

    virtual int send(const void *buffer, size_t length, int flags = 0) override;
    virtual int send(const iovec *buffers, size_t length, int flags = 0) override;
    virtual int sendTo(const void *buffer, size_t length, const Address::ptr to, int flags = 0) override;
    virtual int sendTo(const iovec *buffers, size_t length, const Address::ptr to, int flags = 0) override;

    virtual int recv(void *buffer, size_t length, int flags = 0) override;
    virtual int recv(iovec *buffers, size_t length, int flags = 0) override;
    virtual int recvFrom(void *buffer, size_t length, Address::ptr from, int flags = 0) override;
    virtual int recvFrom(iovec *buffers, size_t length, Address::ptr from, int flags = 0) override;

    // 加载服务器的SSL证书(公匙)和私钥，用于建立加密通信、身份验证
    // 这个函数需要在服务器初始化时调用
    // 服务器必须有一张合法的身份证明也就是证书，以及能证明自己拥有证书的钥匙，也就是私钥
    // cert_file：公钥证书如server.crt
    // key_file：私钥文件如server.key
    // 客户端连接时，服务器会把server.crt发送过去，客户端可以验证这个证书是否合法(通过CA签名)，
    //  然后进行SSL握手，建立加密通道，后续所有send/recv数据都会自动加密解密
    bool loadCertificates(const std::string &cert_file, const std::string &key_file);
    virtual std::ostream &dump(std::ostream &os) const override;

protected:
    virtual bool init(int sock) override;

private:
    // SSL上下文(加载证书/密钥，设定加密算法)
    std::shared_ptr<SSL_CTX> m_ctx;
    // 当前连接的SSL会话，用于实际加解密通信
    std::shared_ptr<SSL> m_ssl;
};

std::ostream &operator<<(std::ostream &os, const Socket &sock);

}

#endif
