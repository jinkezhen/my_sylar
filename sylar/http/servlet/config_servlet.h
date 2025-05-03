#ifndef __SYLAR_HTTP_SERVLETS_CONFIG_SERVLET_H_
#define __SYLAR_HTTP_SERVLETS_CONFIG_SERVLET_H_

#include "sylar/http/servlet.h"
// [客户端浏览器或 curl] 
//         |
//         v
//    发送请求: GET /config
//         |
//         v
//    [服务端 Sylar HTTPServer]
//         |
//         v
//     ServletDispatch 查找路径 /config -> ConfigServlet
//         |
//         v
//     调用: ConfigServlet::handle(request, response, session)
//         |
//         v
//     返回配置内容 -> response
//         |
//         v
//    返回给客户端
//注意：这里的config指的是 Sylar 框架中的全局配置中心 sylar::Config 所注册的所有配置变量，
//这些变量可以是任何模块（比如日志系统、线程池、协程、网络等）在启动或运行期间注册进去的。

namespace sylar {
namespace http {

class ConfigServlet : public Servlet {
public:
    ConfigServlet();
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                            sylar::http::HttpResponse::ptr response,
                            sylar::http::HttpSession::ptr session) override;
}; 

}
}

#endif