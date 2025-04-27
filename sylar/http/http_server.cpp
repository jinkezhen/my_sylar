#include <memory>

#include "http_server.h"
#include "sylar/log.h"
#include "sylar/http/servlets/config_servlet.h"
#include "sylar/http/servlets/status_servlet.h"

namespace sylar {
namespace http {

HttpServer::HttpServer(bool keepalive
                        ,sylar::IOManager* worker
                        ,sylar::IOManager* io_worker
                        ,sylar::IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker),
    m_isKeepalive(keepalive) {
    m_dispatch.reset(new ServletDispatch);
    m_type = "http";
    //将/_/status路径映射到一个StatusServlet实例，当用户访问/_/status时，就会调用StatusServlet::handle()
    m_dispatch->addServlet("/_/status", Servlet::ptr(new StatusServlet));
    m_dispatch->addServlet("/_/config", Servlet::ptr(new ConfigServlet));
}

void HttpServer::setName(const std::string& v) {
    TcpServer::setName(v);
    //设置默认的Servlet，如果用户访问的URL没有任何匹配的Servlet路径，就会调用这个默认处理器来响应
    //这里创建的是一个404处理器
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

//接收请求->分发处理->发送响应
//这里传入的socket是服务端接受客户端连接后返回的套接字，即accept返回的
void HttpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_DEBUG(g_logger) << "handleClient" << *Client;
    HttpSession session(new HttpSession(client));
    do {
        auto req = session->recvRequest();
        if (!req) {
            break;
        }
        //req->isClose()用于判断客户端在一次请求中有没有说：处理完我就关闭连接吧
        //背后是HTTP协议中的Connect字段
        //Connect：close表明客户端请求服务器在响应后关闭连接
        //Connect：keep-alive客户端希望和服务器保持长连接
        HttpResponse::ptr rsp(new HttpResponse(req->getVersion(), req->isClose() || !m_isKeepalive));
        rsp->setHeader("Server", getName());
        m_dispatch->handle(req, rsp, session);
        session->sendResponse(rsp);
        if (!m_isKeepalive || req->isClose()) {
            break;
        }
    } while (true);
    session->close();
}


}
}

