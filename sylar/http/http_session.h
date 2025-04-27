/**
 * @brief HttpSession：是server端accept返回的socket
 * @date 2025-04-22
 * @copyright Copyright (c) All rights reserved.
 */


#ifndef __SYLAR_HTTP_SESSION_H__
#define __SYLAR_HTTP_SESSION_H__

#include <memory>

#include "sylar/streams/socket_stream.h"
#include "socket.h"
#include "http.h"

namespace sylar {
namespace http {

class HttpSession : public SocketStream {
public:
    typedef std::shared_ptr<HttpSession> ptr;

    //owner是否托管
    HttpSession(Socket::ptr sock, bool owner = true);

    //接收Http请求，并从接收的TCP流中解析成HttpRequest
    HttpRequest::ptr recvRequest();
    //将HttpResponse序列化，并通过socket发送出去
    int sendResponse(HttpResponse::ptr rsp);
};

}
}

#endif __SYLAR_HTTP_SESSION_H__