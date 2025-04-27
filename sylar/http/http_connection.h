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
#include "sylar/uri.h"
#include "http.h"

#include <map>
#include <list>
#include <string>
#include <memory>
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




}
}



#endif