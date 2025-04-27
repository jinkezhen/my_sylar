/**
 * @file http_parser.h
 * @brief HTTP协议解析封装
 * @date 2025-04-16
 * @copyright Copyright (c) 2025年  All rights reserved
 */

 #ifndef __SYLAR_HTTP_PARSER_H__
 #define __SYLAR_HTTP_PARSER_H__
 
 #include "http.h"
 #include "http11_parser.h"
 #include "httpclient_parser.h"

 #include <memory>

namespace sylar {
namespace http {

//Http请求解析类，将请求解析成HttpRequest
class HttpRequestParser {
public:
    typedef std::shared_ptr<HttpRequestParser> ptr;
    HttpRequestParser();

    //解析协议，把data中的数据交给http_parser去解析
    //data：由网络层传上来的原始HTTP文本
    //len：这段HTTP文本的内容长度
    //返回实际解析的长度，并将已经解析的数据移除，未解析的部分可以继续传给下次调用
    size_t execute(char* data, size_t len);
    //是否解析完成，对于HTTP请求来说，Header+body(可选)都解析完了才算完成
    int isFinshed();
    //是否有错误
    int hasError();
    //返回解析后的结果(结果存储到HttpRequest类中)
    HttpRequest::ptr getData() const {return m_data;}
    //设置错误
    void setError(int v) {m_error = v;}
    //获取消息体长度，对应HTTP中的Content-length字段，常用于确定是否读完整个body
    uint64_t getContentLength();
    //获取http_parser结构体
    const http_parser& getParser() const {return m_parser;}

public:
    //返回用于存放HTTP请求内容的的缓冲区的大小
    static uint64_t GetHttpRequestBufferSize();
    //返回最大可接受的请求体长度(如64M)，用于防止大流量攻击
    static uint64_t GetHttpRequestMaxBodySize();

private:
    //http_parser,负责实际解析
    http_parser m_parser;
    //HttpRequest结构，解析出来的最终结果
    HttpRequest::ptr m_data;
    //错误码
    //1000：invalid method
    //1001：invalid version
    //1002：invalid field
    int m_error;
};



//Http响应解析类，将请求解析成HttpResponse
class HttpResponseParser {
public:
    typedef std::shared_ptr<HttpResponseParser> ptr;
    HttpResponseParser();

    //解析协议，把data中的数据交给http_parser去解析
    //data：由网络层传上来的原始HTTP文本
    //len：这段HTTP文本的内容长度
    //chunk：是否按块处理http response
    //     补充：http两种传输编码(请求体、响应体)的格式
    //          1.contextContent-Length 客户端在请求头中使用 Content-Length 指定了请求体的 总字节数，服务器根据这个值读取固定长度的内容。
    //              POST /submit HTTP/1.1
    //              Host: example.com
    //              Content-Type: text/plain
    //              Content-Length: 11
    //
    //              Hello World（11）
    //          2.Transfer-Encoding: chunked 客户端将请求体分为多个小块发送，每一块前面加上长度字段，最后用 0\r\n\r\n 表示结束。
    //              POST /upload HTTP/1.1
    //              Host: example.com
    //              Transfer-Encoding: chunked
    //
    //              4\r\n
    //              Wiki\r\n
    //              5\r\n
    //              pedia\r\n
    //              0\r\n
    //              \r\n
    //返回实际解析的长度，并将已经解析的数据移除，未解析的部分可以继续传给下次调用
    size_t execute(char* data, size_t len, bool chunck);
    //是否解析完成，对于HTTP请求来说，Header+body(可选)都解析完了才算完成
    int isFinshed();
    //是否有错误
    int hasError();
    //返回解析后的结果(结果存储到HttpResponse类中)
    HttpResponse::ptr getData() const {return m_data;}
    //设置错误
    void setError(int v) {m_error = v;}
    //获取消息体长度，对应HTTP中的Content-length字段，常用于确定是否读完整个body
    uint64_t getContentLength();
    //获取http_parser结构体
    const http_parser& getParser() const {return m_parser;}

public:
    //返回用于存放HTTP响应内容的的缓冲区的大小
    static uint64_t GetHttpRequestBufferSize();
    //返回最大可接受的响应体长度(如64M)
    static uint64_t GetHttpRequestMaxBodySize();

private:
    //http_parser,负责实际解析
    httpclient_parser m_parser;
    //HttpResponse结构，解析出来的最终结果
    HttpResponse::ptr m_data;
    //错误码
    //1000：invalid method
    //1001：invalid version
    //1002：invalid field
    int m_error;
};

}
}
#endif