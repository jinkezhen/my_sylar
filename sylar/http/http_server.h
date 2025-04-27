/**
 * @file http_server.h
 * @brief HTTP服务器封装
 * @date 2025-04-23
 * @copyright Copyright (c) All rights reserved
 */

 //它继承自TcpServer提供的基本网络监听和客户端连接能力，并在此基础上增加了对HTTP协议的支持

#ifndef __SYLAR_HTTP_SERVER_H__
#define __SYLAR_HTTP_SERVER_H__

#include <memory>

#include "sylar/tcp_server.h"
#include "http_session.h"
#include "servlet.h"

namespace sylar {
namespace http {

class HttpServer : public TcpServer {
public:
    typedef std::shared_ptr<HttpServer> ptr;

    HttpServer(bool keepalive = false, 
                sylar::IOManager* worker = sylar::IOManager::GetThis(),
                sylar::IOManager* io_worker = sylar::IOManager::GetThis(),
                sylar::IOManager* accept_worker = sylar::IOManager::GetThis());

    //返回当前服务器使用的ServletDispath实例，开发者可以用它来注册URL和Servlet的映射
    ServletDispatch::ptr getServletDispatch() const { return m_dispatch; }
    void setServletDispatch(ServletDispatch::ptr v) { m_dispatch = v ;}

    //为服务器设置一个名字
    virtual void setName(const std::string& v) override;
    //客户端处理函数，当有客户端连接时，这个方法会被调用
    virtual void handleClient(Socket::ptr client) override;

private:
    //标识HTTP是否启用长连接
    bool m_isKeepalive;
    //是一个Servlet管理器(调度器)，负责URL到处理器的映射
    //这里的映射指的是根据HTTP请求路径URL选择并调用对应的Servlet(请求处理器)来处理请求
    ServletDispatch::ptr m_dispatch;

};


}
}
 #endif