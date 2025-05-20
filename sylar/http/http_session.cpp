#include <string>
#include "http_session.h"
#include "http_parser.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner = true) :SocketStream(sock, owner) {
    m_socket = sock;
    m_owner = owner;
}

HttpRequest::ptr HttpSession::recvRequest() {
    //该函数的整体流程就是先在socket缓冲区读取完整的HTTP请求数据(包括请求头和请求体)
    //然后将其解析为一个结构化的HttpRequest对象返回。

    HttpRequestParser::ptr parser(new HttpRequestParser);
    //获取初始读取缓冲区大小
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    std::shared_ptr<char> buffer(new char[buff_size], [](char* ptr){
                                    delete[] ptr;
                                });
    //获取裸指针用于操作数据
    char* data = buffer.get();
    //记录已经读入，但未解析的缓冲区位置
    int offset = 0;
    do {
        int len = read(data + offset, buff_size - offset);
        if (len <= 0) {
            close();
            return nullptr;
        }
        len += offset;
        //nparse是解析了多少字节
        size_t nparse = parser->execute(data, len);
        if (parser->hasError()) {
            close();
            return nullptr;
        }
        //将offset设置为未解析部分的字节数
        offset = len - nparse;
        if (offset == (int)buff_size) {
            close();
            return nullptr;
        }
        if (parser->isFinshed()) {
            break;
        }
    } while(true);


    
    //获取请求体长度
    int64_t length = parser->getContentLength();
    if (length > 0) {
        std::string body;
        body.resize(length);
        int len = 0;
        if (length >= offset) { //否则只拷贝offset，剩下的后面再读
            memcpy(&body[0], data, offset);
            len = offset;
        } else { //如果offset已经覆盖了body的长度，就拷贝body
            memcpy(&body[0], data, length);
            len = length;
        }
        length -= offset;
        if (length > 0) {
            if (readFixSize(&body[len], length) <= 0) {
                close();
                return nullptr;
            }
        }
        parser->getData()->setBody(body);
    }
    parser->getData()->init();
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

}
}