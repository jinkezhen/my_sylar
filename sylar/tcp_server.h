/**
 * @file tcp_server.h
 * @brief TCP服务器的封装
 * @date 2025-04-17
 * @copyright Copyright (c) 2025 All rights reserved
 */

//该类的作用
//1.TcpServer可以用来绑定一个或多个IP地址和端口，用于监听来自客户端的请求。
//  绑定成功后，服务器会将这些地址作为入口点，等待客户端通过网络发起TCP连接，
//  这个过程底层通过Socket类封装的bind和listen接口来实现，支持IPV4 IPV6 UNIX
//2.接收客户端连接
//  一旦TcpServer开始监听，TcpServer内部的调度器IOManger
//  不断调用accept()来接收新的客户端连接请求，每当有客户端发起连接并成功建立TCP三次握手后
//  服务器就会自动生成一个新的Socket对象来代表该客户端
//3.创建会话（Session）
//  对于每一个成功接入的客户端连接，TcpServer 会通过协程机制为其创建一个独立的“会话”，
//  由一个协程负责该客户端的通信逻辑处理。这种结构实现了并发连接处理，
//  同时避免了传统多线程编程中的线程资源消耗与上下文切换开销。
//4.分发数据处理任务
//  TcpServer 提供了一个虚函数 handleClient()，用于接收和处理客户端发送的数据。
//  开发者通常会继承 TcpServer 并重写这个方法，实现自定义的协议解析与业务逻辑处理。
//  例如，在 HTTP 服务中，这里会解析 HTTP 请求并生成响应。通过这种方式，
//  TcpServer 实现了通信层和业务层的解耦。



#ifndef __SYLAR_TCP_SERVER_H__
#define __SYLAR_TCP_SERVER_H__

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <yaml-cpp/yaml.h>
#include "address.h"
#include "iomanager.h"
#include "socket.h"
#include "noncopyable.h"
#include "config.h"

namespace sylar {


//该结构体用于配置TcpServer
struct TcpServerConf {
    typedef std::shared_ptr<TcpServerConf> ptr;

    //服务器监听的地址列表
    std::vector<std::string> address;
    //是否启用长连接,0表示禁用
    int keepalive = 0;
    //连接超时时间，默认为两分钟
    int timeout = 60 * 1000 * 2;
    //是否启用ssl/tls
    int ssl;
    //服务器的唯一id
    std::string id;
    //服务器类型,如http ws rock
    std::string type = "http";
    //服务器名称
    std::string name;
    //启用ssl后的，ssl证书路径、私钥路径
    std::string cert_file;
    std::string key_file;
    //负责接收新连接的线程或工作器
    std::string accept_worker;
    //负责io操作的工作器名称
    std::string io_worker;
    //负责业务逻辑处理的工作器名称
    std::string process_worker;
    //其他扩展参数
    std::map<std::string, std::string> args;

    bool isValid() const {
        return !address.empty();
    }

    bool operator==(const TcpServerConf& oth) const {
        return address == oth.address
            && keepalive == oth.keepalive
            && timeout == oth.timeout
            && name == oth.name
            && ssl == oth.ssl
            && cert_file == oth.cert_file
            && key_file == oth.key_file
            && accept_worker == oth.accept_worker
            && io_worker == oth.io_worker
            && process_worker == oth.process_worker
            && args == oth.args
            && id == oth.id
            && type == oth.type;
    }
};

//以下两个模板函数是LexicalCast的特化版本，用于实现TcpServerConf结构体
//与std::string(通常是YAML/JSON格式的字符串)之间的双向转换，用于配置文件的解析
template<>  //当我们想要对一个通用模板类进行类型特化时必须先显示写出template<>表示这是一个对通用模板类的特化版本
class LexicalCast<std::string, TcpServerConf> {
public:
    TcpServerConf operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        TcpServerConf conf;
        conf.id = node["id"].as<std::string>(conf.id); //如果传入的string中不存在id字段，则使用conf.id的默认值
        conf.type = node["type"].as<std::string>(conf.type);
        conf.keepalive = node["keepalive"].as<int>(conf.keepalive);
        conf.timeout = node["timeout"].as<int>(conf.timeout);
        conf.name = node["name"].as<std::string>(conf.name);
        conf.ssl = node["ssl"].as<int>(conf.ssl);
        conf.cert_file = node["cert_file"].as<std::string>(conf.cert_file);
        conf.key_file = node["key_file"].as<std::string>(conf.key_file);
        conf.accept_worker = node["accept_worker"].as<std::string>();
        conf.io_worker = node["io_worker"].as<std::string>();
        conf.process_worker = node["process_worker"].as<std::string>();
        conf.args = LexicalCast<std::string
            ,std::map<std::string, std::string> >()(node["args"].as<std::string>(""));
        if(node["address"].IsDefined()) {
            for(size_t i = 0; i < node["address"].size(); ++i) {
                conf.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return conf;
    }
};

template<>
class LexicalCast<TcpServerConf, std::string> {
public:
    std::string operator()(const TcpServerConf& conf) {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["name"] = conf.name;
        node["keepalive"] = conf.keepalive;
        node["timeout"] = conf.timeout;
        node["ssl"] = conf.ssl;
        node["cert_file"] = conf.cert_file;
        node["key_file"] = conf.key_file;
        node["accept_worker"] = conf.accept_worker;
        node["io_worker"] = conf.io_worker;
        node["process_worker"] = conf.process_worker;
        node["args"] = YAML::Load(LexicalCast<std::map<std::string, std::string>
            , std::string>()(conf.args));
        for(auto& i : conf.address) {
            node["address"].push_back(i);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//TcpServer服务器封装
class TcpServer : public std::enable_shared_from_this<TcpServer>, Noncopyable {
public:
    //worker：执行业务逻辑
    //io_worker：执行io操作
    //accept_worker：执行接收连接的操作
    //通过分离三种操作，提升服务器的并发性能和资源利用率
    TcpServer::TcpServer(sylar::IOManager* worker = sylar::IOManager::GetThis(),
                         sylar::IOManager* io_worker = sylar::IOManager::GetThis(),
                         sylar::IOManager* accept_worker = sylar::IOManager::GetThis());
    virtual ~TcpServer();

    //绑定地址,返回是否绑定成功
    virtual bool bind(sylar::Address::ptr addr, bool ssl = false);
    //绑定地址数组，返回绑定成功与否的同时返回可能绑定失败的地址
    virtual bool bind(const std::vector<Address::ptr>& addrs,
                      const std::vector<Address::ptr>& fails,
                      bool ssl = false);
    //加载证书
    bool loadCertificates(const std::string& cert_file, const std::string& key_file);

    //启动服务，需在绑定成功后执行
    virtual bool start();
    //停止服务
    virtual void stop();

    //返回读取超时时间(毫秒)
    uint64_t getRecvTimeout() const { return m_recvTimeout;}

    //返回服务器名称
    std::string getName() const { return m_name;}

    //设置读取超时时间(毫秒)
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v;}

    //设置服务器名称
    virtual void setName(const std::string& v) { m_name = v;}

    //是否停止
    bool isStop() const { return m_isStop;}

    TcpServerConf::ptr getConf() const { return m_conf;}
    void setConf(TcpServerConf::ptr v) { m_conf = v;}
    void setConf(const TcpServerConf& v);

    virtual std::string toString(const std::string& prefix = "");

    std::vector<Socket::ptr> getSocks() const { return m_socks;}


protected:
    //负责处理新建立的客户端连接(由服务端accept返回的socket)
    //当以下事件发生时，该函数被触发
    //1.客户端通过connect()连接到服务器的监听端口比如80
    //2.m_accpetWorker调用accept接受连接，生成一个新的Socket::ptr(客户端套接字)
    //3.服务器将该套接字传递给handleClient，进入实际业务处理逻辑
    virtual void handleClient(Socket::ptr client);
    //开始接受连接
    virtual void startAccept(Socket::ptr sock);
    
protected:
    //监听socket数组,注意这里存储的是当前服务器监听的IP:port(自己的服务器上的)，而不是已连接的远端socket
    //为什么这样设计是因为服务器可能需要同时监听多个自身端口，如http的80，https的443
    std::vector<Socket::ptr> m_socks;
    //新连接的socket工作的调度器
    IOManager* m_worker;    //专门处理业务逻辑
    IOManager* m_ioWorker;  //专门处理socket的收发
    //服务器socket接收连接的调度器
    IOManager* m_acceptWorker; //
    //接收超时时间
    uint64_t m_recvTimeout;
    //服务器名称
    std::string m_name;
    //服务器类型
    std::string m_type = "tcp";
    //服务是否停止
    bool m_isStop;
    //是否启用SSL/TLS
    bool m_ssl = false;

    TcpServerConf::ptr m_conf;
};


}
#endif