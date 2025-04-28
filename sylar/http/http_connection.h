/**
 * @file http_connection.h
 * @brief httpconnection是client端connect返回的scoket
 * @date 2025-04-26
 * @copyright Copyright (C) All rights reserved
 */

#ifndef __SYLAR_HTTP_CONNECTION_H__
#define __SYLAR_HTTP_CONNECTION_H__

#include "sylar/streams/socket_stream.h"
#include "sylar/thread.h"
#include "sylar/mutex.h"
#include "sylar/uri.h"
#include "http.h"

#include <map>
#include <list>
#include <string>
#include <memory>
#include <atomic>
#include <stdint.h>

namespace sylar {
namespace http {

//http响应结果
//该结构体的作用是封装客户端一次http请求的结果信息
struct HttpResult {
    typedef std::shared_ptr<HttpResult> ptr;
    // 特性	普通枚举（enum）	枚举类（enum class）
    // 作用域	成员暴露在外部作用域中	成员限定在枚举名的作用域内
    // 隐式转换	可隐式转换为整数（int）	不可隐式转换为整数
    // 类型安全	无类型安全，枚举值可能冲突	类型安全，互不冲突
    // 底层类型指定	不支持（C++11 前）	支持指定底层类型
    // 命名污染	有，容易和其他标识符冲突	无，名字封装在作用域中
    // 向前兼容性	与 C 语言兼容	仅 C++ 支持（C++11 起）
    enum class Error {
        //正常
        OK = 0,
        //非法Uri
        INVALID_URI = 1,
        //无法解析HOST
        INVALID_HOST = 2,
        //连接失败
        CONNECT_FAIL = 3,
        //连接对对端关闭
        SEND_CLOSE_BY_PEER = 4,
        //发送请求产生socket错误
        SEND_SOCKET_ERROR = 5,
        //超时
        TIMEOUT = 6,
        //创建socket失败
        CREATE_SOCKET_ERROR = 7,
        //从连接池中连接失败
        POOL_GET_CONNECTION = 8,
        //无效的连接
        POOL_INVALID_CONNECTION = 9
    };

    HttpResult(int _result, HttpResponse::ptr _response, const std::string& _error) : 
        result(_result), response(_response), error(_error){
    }
    int result; //错误码
    HttpResponse::ptr response; //Http响应结构体
    std::string error;
    std::string toString() const;
};


class HttpConnectionPool;

// GET 请求
// 作用：GET 用于从服务器获取数据，不会修改服务器资源，是只读操作。
// 使用场景：适用于查询数据、搜索请求、加载网页等无副作用的操作，参数通过URL传递（如 ?id=123），适合简单、公开的数据请求。
// POST 请求
// 作用：POST 用于向服务器提交数据，通常用于创建、更新或删除资源，可能改变服务器状态。
// 使用场景：适用于表单提交、文件上传、登录认证等需要传输敏感或大量数据的情况，数据放在请求体中，不会暴露在URL上，安全性更高。

//HTTP客户端类
class HttpConnection : public SocketStream {
friend class HttpConnectionPool;
public:
    typedef std::shared_ptr<HttpConnection> ptr;

    //Do...函数内部实现了 发送请求（sendRequest）和 接收响应（recvResponse）
    //发送HTTP的GET请求
    //uri：请求的uri
    //timeout：请求超时时间
    //headers：请求头部参数
    //body：请求消息体
    //返回该次请求的结果结构体
    static HttpResult::ptr DoGet(const std::string& url, uint64_t timeout_ms, 
                                 const std::map<std::string, std::string>& headers = {},
                                 const std::string& body = "");
    static HttpResult::ptr DoGet(Uri::ptr uri, uint64_t timeout_ms, 
                                 const std::map<std::string, std::string>& headers = {},
                                 const std::string& body = "");
    //发送HTTP的POST请求
    static HttpResult::ptr DoPost(const std::string& url, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");
    static HttpResult::ptr DoPost(Uri::ptr uri, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");

    //发送HTTP请求
    static HttpResult::ptr DoRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, 
                                     const std::map<std::string, std::string>& headers = {},
                                     const std::string& body = "");
    static HttpResult::ptr DoRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, 
                                     const std::map<std::string, std::string>& headers = {},
                                     const std::string& body = "");
    static HttpResult::ptr DoRequest(HttpRequest::ptr req, Uri::ptr uri, uint64_t timeout_ms);

    HttpConnection(Socket::ptr sock, bool owner = true);
    ~HttpConnection();

    //接收HTTP响应
    HttpResponse::ptr recvResponse();
    //发送HTTP请求
    int sendRequest(HttpRequest::ptr req);

public:
    // 记录当前 HttpConnection 对象（即一个 HTTP 客户端连接）的创建时间点（通常是毫秒或微秒级时间戳）。
    uint64_t m_createTime = 0;
    // 统计当前连接已发送的HTTP请求数量（每次执行完DoRequest或sendRequest后计数+1）。
    uint64_t m_request = 0;
};


//该类是Http连接池，用来统一管理、复用、维护一批HTTP链接，避免每次请求都新建连接，提高性能，节省资源
//前提是HTTP请求服务器和客户端都支持连接复用机制(keep-aive机制)，那么
// 第一次建立 TCP 连接之后（socket 建好了），
// 后续可以在这个连接上连续发送多个 HTTP 请求，
// 只要连接有效、没有关闭，就可以从连接池拿出用一次，然后放回池子继续用，
// 这样就省掉了反复建连接的开销（TCP三次握手、SSL握手都很重）。
// 这就是连接池存在的意义。
class HttpConnectionPool {
public:
    typedef std::shared_ptr<HttpConnectionPool> ptr;
    typedef Mutex MutexType;

    //创建一个连接池
    static HttpConnectionPool::ptr Create(const std::string& uri, const std::string& vhost,
                                          uint32_t max_size, uint32_t max_alive_time, uint32_t max_request);

    HttpConnectionPool(const std::string& host
                      ,const std::string& vhost
                      ,uint32_t port
                      ,bool is_https
                      ,uint32_t max_size
                      ,uint32_t max_alive_time
                      ,uint32_t max_request);
    
    //从连接池中拿到一个可用的HttpConnection，如果没有可用的就创建新的连接
    HttpConnection::ptr getConnection();

    HttpResult::ptr doGet(const std::string& url, uint64_t timeout_ms, 
                                 const std::map<std::string, std::string>& headers = {},
                                 const std::string& body = "");
    HttpResult::ptr doGet(Uri::ptr uri, uint64_t timeout_ms, 
                                 const std::map<std::string, std::string>& headers = {},
                                 const std::string& body = "");
    //发送HTTP的POST请求
    HttpResult::ptr doPost(const std::string& url, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");
    HttpResult::ptr doPost(Uri::ptr uri, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");
    //发送HTTP请求
    HttpResult::ptr doRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");
    HttpResult::ptr doRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, 
                                  const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");
    HttpResult::ptr doRequest(HttpRequest::ptr req, uint64_t timeout_ms);
        
    


private:
    //当一个HttpConnection(HTTP连接)用完后，不是直接销毁，而是通过ReleasePtr把它归还到连接池中或者彻底销毁
    static void ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool); 
private:
    //保存远程服务器的的主机名或ip地址,比如请求 http://example.com/path，那么 m_host 就是 example.com。
    std::string m_host;
    //保存虚拟主机名,有些服务器（比如 Apache/Nginx）在同一个 IP 地址上跑多个网站，
    //靠 Host 头区分，比如访问 www.site1.com 和 www.site2.com，虽然是同一个 IP，
    //但需要发送不同的 Host。那么这里的 m_vhost 保存的就是你要访问的虚拟主机名
    std::string m_vhost;
    //保存目标服务器的端口号
    uint32_t m_port;
    //控制连接池最多可以同时拥有多少个连接
    uint32_t m_maxSize;
    //每一个连接最多存活多久
    uint32_t m_maxAliveTime;
    //单个连接最多使用多少次后强制关闭重建
    uint32_t m_maxRequest;
    //标记是否是加密传输
    bool m_isHttps;
    MutexType m_mutex;
    //存储可用的空闲连接列表
    std::list<HttpConnection*> m_conns;
    //统计当前连接池中总共存在的连接数
    //假设m_conns中有三个空闲的连接，还有两个正在使用的连接，那么m_total=5
    std::atomic<int32_t> m_total = {0};
};

}
}



#endif