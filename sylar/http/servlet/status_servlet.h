#ifndef __SYLAR_HTTP_SERVLETS_STATUS_SERVLET_H_
#define __SYLAR_HTTP_SERVLETS_STATUS_SERVLET_H_

#include "sylar/http/servlet.h"

// 它是 HTTP 层提供的一个 Servlet（处理器）；
// 具体是处理 /status 或类似路径的请求；
// 主要作用是：返回系统运行状态，比如线程信息、协程数量、连接数、负载等。
namespace sylar {
namespace http {

class StatusServlet : public Servlet {
public:
    StatusServlet();
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                        , sylar::http::HttpResponse::ptr response
                        , sylar::http::HttpSession::ptr session) override;
};

}
}

#endif