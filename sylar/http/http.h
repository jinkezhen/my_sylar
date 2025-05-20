/**
 * @file http.h
 * @brief http定义结构体封装
 * @date 2025-04-13
 * @copyright Copyright (c) 2025年 All rights reserved
 */

#ifndef __SYLAR_HTTP_HTTP_H__
#define __SYLAR_HTTP_HTTP_H__

#include <memory>
#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <boost/lexical_cast.hpp>


// HTTP（超文本传输协议）是一种客户端（比如浏览器、移动 App）和服务器之间通信的协议。
// 客户端发送“请求”，服务器返回“响应”。

// 一个 HTTP 请求主要由三部分组成：
// 请求行（Request Line）
// 请求头（Request Headers）
// 请求体（Request Body）【可选，通常出现在 POST 等方法中】

// ✅ 示例：GET 请求（浏览网页）
// GET /index.html HTTP/1.1   请求行：其中GET是请求的方法，/index.html是要访问的资源路径，HTTP/1.1是使用的HTTP协议版本
// Host: www.example.com      下面三行都是请求头，用来传附加信息，比如你用什么浏览器、你想要什么格式的响应等
// User-Agent: Mozilla/5.0    
// Accept: text/html          

// ✅ 示例：POST 请求（提交表单）
// POST /login HTTP/1.1        请求行：提交数据
// Host: www.example.com
// Content-Type: application/x-www-form-urlencoded
// Content-Length: 29
// username=admin&password=1234  请求体：包含我们要提交的内容

// 一个 HTTP 响应也主要由三部分组成：
// 响应行（Status Line）
// 响应头（Headers）
// 响应体（Body）

// ✅ 示例：200 OK 响应
// HTTP/1.1 200 OK            响应行：200表示请求成功，响应了请求成功状态码200
// Content-Type: text/html
// Content-Length: 1234
// <html>                     响应体：就是给我们看的网页内容
//   <body>Hello World!</body>
// </html>




namespace sylar {
namespace http {

/* Request Methods */
#define HTTP_METHOD_MAP(XX)                                      \
  /* DELETE：请求服务器删除指定的资源 */                        \
  XX(0, DELETE, DELETE)                                    \
  /* GET：请求指定资源的表现形式，是最常见的请求方法 */        \
  XX(1, GET,  GET)                                       \
  /* HEAD：与 GET 类似，但不返回消息体，只获取响应头信息 */     \
  XX(2, HEAD, HEAD)                                      \
  /* POST：向服务器提交数据（如表单、上传文件） */              \
  XX(3, POST, POST)                                      \
  /* PUT：向指定资源位置上传其最新内容（整体替换） */           \
  XX(4, PUT, PUT)                                       \
  /* pathological */                                             \
  /* CONNECT：建立隧道，用于 HTTPS 代理等场景 */                \
  XX(5, CONNECT, CONNECT)                                   \
  /* OPTIONS：获取目标资源支持的 HTTP 方法 */                   \
  XX(6, OPTIONS, OPTIONS)                                   \
  /* TRACE：回显收到的请求，主要用于诊断测试 */                \
  XX(7, TRACE, TRACE)                                     \
  /* WebDAV */                                                   \
  /* COPY：复制资源（WebDAV 扩展） */                           \
  XX(8, COPY, COPY)                                      \
  /* LOCK：对资源加锁，避免并发冲突（WebDAV 扩展） */           \
  XX(9, LOCK, LOCK)                                      \
  /* MKCOL：创建一个集合（如文件夹，WebDAV 扩展） */            \
  XX(10, MKCOL, MKCOL)                                     \
  /* MOVE：移动资源，相当于剪切+粘贴（WebDAV 扩展） */          \
  XX(11, MOVE, MOVE)                                      \
  /* PROPFIND：获取资源的属性（如文件元数据） */                \
  XX(12, PROPFIND, PROPFIND)                                  \
  /* PROPPATCH：更新资源的属性（类似设置文件信息） */           \
  XX(13, PROPPATCH, PROPPATCH)                                 \
  /* SEARCH：对资源集合进行搜索（WebDAV 扩展） */               \
  XX(14, SEARCH, SEARCH)                                    \
  /* UNLOCK：释放资源锁（对应 LOCK 方法） */                    \
  XX(15, UNLOCK, UNLOCK)                                    \
  /* BIND：为资源创建一个新路径（类似软链接） */                \
  XX(16, BIND, BIND)                                      \
  /* REBIND：替换资源路径（同时删除旧绑定） */                  \
  XX(17, REBIND, EBIND)                                    \
  /* UNBIND：移除绑定路径（不一定删除资源） */                  \
  XX(18, UNBIND, UNBIND)                                    \
  /* ACL：设置访问控制权限（WebDAV 扩展） */                    \
  XX(19, ACL,  ACL)                                       \
  /* subversion */                                               \
  /* REPORT：版本控制中用于获取报告（如提交历史） */            \
  XX(20, REPORT, REPORT)                                    \
  /* MKACTIVITY：创建活动资源，用于事务控制 */                  \
  XX(21, MKACTIVITY, MKACTIVITY)                                \
  /* CHECKOUT：检出资源的版本，用于后续编辑 */                  \
  XX(22, CHECKOUT, CHECKOUT)                                  \
  /* MERGE：合并资源变更，常用于版本控制 */                     \
  XX(23, MERGE, MERGE)                                     \
  /* upnp */                                                     \
  /* M-SEARCH：用于发现局域网中的设备（UPnP 协议） */           \
  XX(24, MSEARCH, M-SEARCH)                                  \
  /* NOTIFY：事件通知，用于设备状态变更（UPnP） */              \
  XX(25, NOTIFY, NOTIFY)                                    \
  /* SUBSCRIBE：订阅设备事件通知（UPnP） */                     \
  XX(26, SUBSCRIBE, SUBSCRIBE)                                 \
  /* UNSUBSCRIBE：取消设备事件订阅（UPnP） */                   \
  XX(27, UNSUBSCRIBE, UNSUBSCRIBE)                               \
  /* RFC-5789 */                                                 \
  /* PATCH：对资源进行局部更新（相比 PUT 更轻量） */            \
  XX(28, PATCH, PATCH)                                     \
  /* PURGE：清除缓存（常用于反向代理服务器如 Varnish） */        \
  XX(29, PURGE, PURGE)                                     \
  /* CalDAV */                                                   \
  /* MKCALENDAR：创建日历集合（CalDAV 扩展） */                 \
  XX(30, MKCALENDAR,  MKCALENDAR)                                \
  /* RFC-2068, section 19.6.1.2 */                               \
  /* LINK：给资源添加链接关系（超媒体语义） */                  \
  XX(31, LINK, LINK)                                      \
  /* UNLINK：移除资源之间的链接关系 */                          \
  XX(32, UNLINK, UNLINK)                                    \
  /* icecast */                                                  \
  /* SOURCE：向媒体服务器发送音频流（如 Icecast） */             \
  XX(33, SOURCE, SOURCE)                                    \


/* Status Codes */
#define HTTP_STATUS_MAP(XX)                                                           \
  /* 100 Continue：继续请求，客户端应继续发送剩余部分 */                             \
  XX(100, CONTINUE,                        Continue)                                   \
  /* 101 Switching Protocols：协议切换，服务器同意更改协议 */                         \
  XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)                        \
  /* 102 Processing：WebDAV 请求正在处理中，还未完成 */                               \
  XX(102, PROCESSING,                      Processing)                                 \
  /* 200 OK：请求成功，一般用于 GET 和 POST 请求 */                                  \
  XX(200, OK,                              OK)                                         \
  /* 201 Created：资源创建成功，常用于 POST */                                        \
  XX(201, CREATED,                         Created)                                    \
  /* 202 Accepted：请求已接受，但还未处理完成 */                                     \
  XX(202, ACCEPTED,                        Accepted)                                   \
  /* 203 Non-Authoritative Information：返回的信息不是来自原始服务器 */               \
  XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)              \
  /* 204 No Content：请求成功但无返回内容 */                                          \
  XX(204, NO_CONTENT,                      No Content)                                 \
  /* 205 Reset Content：请求成功，要求重置文档视图 */                                \
  XX(205, RESET_CONTENT,                   Reset Content)                              \
  /* 206 Partial Content：成功处理部分 GET 请求 */                                   \
  XX(206, PARTIAL_CONTENT,                 Partial Content)                            \
  /* 207 Multi-Status：WebDAV 多状态响应 */                                          \
  XX(207, MULTI_STATUS,                    Multi-Status)                               \
  /* 208 Already Reported：WebDAV 成果已在之前响应中报告 */                          \
  XX(208, ALREADY_REPORTED,                Already Reported)                           \
  /* 226 IM Used：服务器满足 GET 请求并使用了实例操作 */                             \
  XX(226, IM_USED,                         IM Used)                                    \
  /* 300 Multiple Choices：有多个可选资源 */                                          \
  XX(300, MULTIPLE_CHOICES,                Multiple Choices)                           \
  /* 301 Moved Permanently：永久重定向 */                                            \
  XX(301, MOVED_PERMANENTLY,               Moved Permanently)                          \
  /* 302 Found：临时重定向（原为 Moved Temporarily） */                              \
  XX(302, FOUND,                           Found)                                      \
  /* 303 See Other：参见其他资源，通常用于 POST 重定向 */                            \
  XX(303, SEE_OTHER,                       See Other)                                  \
  /* 304 Not Modified：资源未修改，可使用缓存 */                                     \
  XX(304, NOT_MODIFIED,                    Not Modified)                               \
  /* 305 Use Proxy：请求必须通过代理访问（已弃用） */                                \
  XX(305, USE_PROXY,                       Use Proxy)                                  \
  /* 307 Temporary Redirect：临时重定向，请求方法不变 */                             \
  XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)                         \
  /* 308 Permanent Redirect：永久重定向，请求方法不变 */                             \
  XX(308, PERMANENT_REDIRECT,              Permanent Redirect)                         \
  /* 400 Bad Request：请求格式错误，服务器无法理解 */                                \
  XX(400, BAD_REQUEST,                     Bad Request)                                \
  /* 401 Unauthorized：请求需要身份验证 */                                           \
  XX(401, UNAUTHORIZED,                    Unauthorized)                               \
  /* 402 Payment Required：预留状态码，代表付款需求 */                               \
  XX(402, PAYMENT_REQUIRED,                Payment Required)                           \
  /* 403 Forbidden：服务器拒绝请求，无权限访问 */                                    \
  XX(403, FORBIDDEN,                       Forbidden)                                  \
  /* 404 Not Found：资源未找到 */                                                   \
  XX(404, NOT_FOUND,                       Not Found)                                  \
  /* 405 Method Not Allowed：请求方法不被允许 */                                     \
  XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)                         \
  /* 406 Not Acceptable：服务器无法满足请求的内容协商 */                              \
  XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                             \
  /* 407 Proxy Authentication Required：需要代理身份验证 */                          \
  XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)              \
  /* 408 Request Timeout：请求超时 */                                                \
  XX(408, REQUEST_TIMEOUT,                 Request Timeout)                            \
  /* 409 Conflict：请求与当前资源状态冲突 */                                         \
  XX(409, CONFLICT,                        Conflict)                                   \
  /* 410 Gone：资源已永久删除，不再可用 */                                           \
  XX(410, GONE,                            Gone)                                       \
  /* 411 Length Required：缺少 Content-Length 头部 */                                \
  XX(411, LENGTH_REQUIRED,                 Length Required)                            \
  /* 412 Precondition Failed：请求头条件失败 */                                     \
  XX(412, PRECONDITION_FAILED,             Precondition Failed)                        \
  /* 413 Payload Too Large：请求体过大 */                                            \
  XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)                          \
  /* 414 URI Too Long：URI 过长，服务器无法处理 */                                   \
  XX(414, URI_TOO_LONG,                    URI Too Long)                               \
  /* 415 Unsupported Media Type：媒体类型不支持 */                                   \
  XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)                     \
  /* 416 Range Not Satisfiable：范围请求超出资源范围 */                              \
  XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)                      \
  /* 417 Expectation Failed：Expect 请求头无法满足 */                                \
  XX(417, EXPECTATION_FAILED,              Expectation Failed)                         \
  /* 421 Misdirected Request：请求发往了错误的服务器 */                              \
  XX(421, MISDIRECTED_REQUEST,             Misdirected Request)                        \
  /* 422 Unprocessable Entity：实体语义错误，无法处理 */                             \
  XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)                       \
  /* 423 Locked：资源已被锁定（WebDAV） */                                           \
  XX(423, LOCKED,                          Locked)                                     \
  /* 424 Failed Dependency：由于前一个请求失败而导致当前请求失败 */                  \
  XX(424, FAILED_DEPENDENCY,               Failed Dependency)                          \
  /* 426 Upgrade Required：客户端应升级协议（如 HTTP 升为 HTTPS） */                 \
  XX(426, UPGRADE_REQUIRED,                Upgrade Required)                           \
  /* 428 Precondition Required：要求请求包含条件头 */                                \
  XX(428, PRECONDITION_REQUIRED,           Precondition Required)                      \
  /* 429 Too Many Requests：请求过多，被限流 */                                       \
  XX(429, TOO_MANY_REQUESTS,               Too Many Requests)                          \
  /* 431 Request Header Fields Too Large：请求头字段太大 */                          \
  XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large)            \
  /* 451 Unavailable For Legal Reasons：因法律原因不可用（如被封锁） */              \
  XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)              \
  /* 500 Internal Server Error：服务器内部错误 */                                     \
  XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)                      \
  /* 501 Not Implemented：服务器不支持该请求方法 */                                  \
  XX(501, NOT_IMPLEMENTED,                 Not Implemented)                            \
  /* 502 Bad Gateway：网关错误，服务器作为网关时收到无效响应 */                      \
  XX(502, BAD_GATEWAY,                     Bad Gateway)                                \
  /* 503 Service Unavailable：服务器当前不可用（超载或维护） */                      \
  XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)                        \
  /* 504 Gateway Timeout：网关超时 */                                                \
  XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                            \
  /* 505 HTTP Version Not Supported：不支持的 HTTP 版本 */                           \
  XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)                 \
  /* 506 Variant Also Negotiates：服务器存在内容协商循环 */                           \
  XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)                    \
  /* 507 Insufficient Storage：服务器无法存储完成请求所需内容（WebDAV） */           \
  XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)                       \
  /* 508 Loop Detected：检测到无限循环（WebDAV） */                                   \
  XX(508, LOOP_DETECTED,                   Loop Detected)                              \
  /* 510 Not Extended：请求需要进一步扩展 */                                          \
  XX(510, NOT_EXTENDED,                    Not Extended)                               \
  /* 511 Network Authentication Required：需要网络认证（如登录 Wi-Fi 门户） */       \
  XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required)            \


//HTTP方法枚举
enum class HttpMethod {
#define XX(num, name, string) name = num,
    HTTP_METHOD_MAP(XX)
#undef XX
    INVALID_METHOD
};

//HTTP状态枚举
enum class HttpStatus {
#define XX(code, name, desc) name = code,
    HTTP_STATUS_MAP(XX)
#undef XX
};

//将字符串方法名转为HTTP方法枚举
HttpMethod StringToHttpMethod(const std::string& m);
//将字符串指针转为HTTP方法枚举
HttpMethod CharsToHttpMethod(const char* m);
//将HTTP方法枚举转为字符串
const char* HttpMethodToString(const HttpMethod& m);
//将HTTP枚举状态转为字符串
const char* HttpStatusToString(const HttpStatus& s);


//仿函数：C++ 中的 仿函数（也叫做“函数对象”）是一个重载了 operator() 的类或结构体。换句话说，仿函数是一个可以像普通函数一样被调用的对象。仿函数常常用于 STL（标准模板库）算法中，作为回调函数或者自定义操作。
// 1.定义一个仿函数
// class Add {
// public:
//     // 重载 operator()
//     int operator()(int a, int b) {
//         return a + b;
//     }
// };
// int main() {
//     Add add;  // 创建仿函数对象
//     cout << add(3, 4) << endl;  // 使用仿函数，等同于调用 add.operator()(3, 4)
//     return 0;
// }
// 2.示例：带状态的仿函数
// class Filter {
//   private:
//       int threshold;  // 状态：过滤的阈值
//   public:
//       Filter(int t) : threshold(t) {}
//       bool operator()(int a) {  // 判断数字是否大于阈值
//           return a > threshold;
//       }
//   };
// 3.在 STL 算法中的应用
// 仿函数常用于 STL 的算法中，比如排序、查找等。例如：
// 定义一个仿函数来比较两个整数的大小
// class Compare {
// public:
//     bool operator()(int a, int b) {
//         return a > b;  // 逆序排列
//     }
// };
// int main() {
//     vector<int> v = {1, 4, 2, 8, 3};
//     sort(v.begin(), v.end(), Compare());  // 使用仿函数进行排序
//     return 0;
// }

struct CaseInsensitiveLess {
    //忽略大小写，比较字符串
    bool operator()(const std::string& lhs, const std::string& rhs) const;
};

/**
 * @brief 获取Map中的key值,并转成对应类型,返回是否成功
 * @param[in] m Map数据结构
 * @param[in] key 关键字
 * @param[out] val 保存转换后的值
 * @param[in] def 默认值
 * @return
 *      @retval true 转换成功, val 为对应的值
 *      @retval false 不存在或者转换失败 val = def
 */
template <class MapType, class T>
bool checkGetAs(const MapType& m, const std::string& key, T& val, const T& def = T()) {
    auto it = m.find(key);
    if (it == m.end()) {
      val = def;
      return false;
    }
    try {
      //这里为什么要用boost::lexical_cast<T>(it->second),因为他可以进行字符串和int、float、bool等类型之间的转换
      //而static_cast只能在相关类型之间转换，比如float->int void*->int* son*->father*
      //而dynamic_cast适用范围更窄，只适应于转换类层级之间的指针或引用且必须有虚函数(即多态类型的指针/引用)
      val = boost::lexical_cast<T>(it->second);
      return true;
    } catch(...) {
      val = def;
    }
    return false;
}

/**
 * @brief 获取Map中的key值,并转成对应类型
 * @param[in] m Map数据结构
 * @param[in] key 关键字
 * @param[in] def 默认值
 * @return 如果存在且转换成功返回对应的值,否则返回默认值
 */
template <class MapType, class T>
T getAs(const MapType& m, const std::string& key, const T& def = T()) {
    auto it = m.find(key);
    if (it == m.end()) {
      return def;
    }
    try {
      return boost::lexical_cast<T>(it->second);
    } catch(...){
    }
    return def;
}


class HttpResponse;
//HTTP请求结构
class HttpRequest {
public:
  typedef std::shared_ptr<HttpRequest> ptr;
  typedef std::map<std::string, std::string, CaseInsensitiveLess()> MapType;

  //构造函数
  //close：是否在请求完毕后关闭TCP连接
  //长连接：Keep-Alive
  //HTTP协议原本是无连接，无状态的也就是说一个请求就建立一次TCP连接，请求完就断开，这就是早期的行为(HTTP1.0默认如此)
  //但频繁的建立连接效率太低，于是引入了keep-alive，http1.0默认是connection:close但可以加connection：keep-alive来来开启长连接
  //http1.1默认是connection:keep-alive,除非显式指定connection:close
  HttpRequest(uint8_t version = 0x11, bool close = true);

  //基于当前请求，创建一个响应对象
  std::shared_ptr<HttpResponse> createResponse();

  /**
   * @brief 返回HTTP方法
   */
  HttpMethod getMethod() const { return m_method;}

  /**
   * @brief 返回HTTP版本
   */
  uint8_t getVersion() const { return m_version;}

  /**
   * @brief 返回HTTP请求的路径
   */
  const std::string& getPath() const { return m_path;}

  /**
   * @brief 返回HTTP请求的查询参数
   */
  const std::string& getQuery() const { return m_query;}

  /**
   * @brief 返回HTTP请求的消息体
   */
  const std::string& getBody() const { return m_body;}

  /**
   * @brief 返回HTTP请求的消息头MAP
   */
  const MapType& getHeaders() const { return m_headers;}

  /**
   * @brief 返回HTTP请求的参数MAP
   */
  const MapType& getParams() const { return m_params;}

  /**
   * @brief 返回HTTP请求的cookie MAP
   */
  const MapType& getCookies() const { return m_cookies;}

  /**
   * @brief 设置HTTP请求的方法名
   * @param[in] v HTTP请求
   */
  void setMethod(HttpMethod v) { m_method = v;}

  /**
   * @brief 设置HTTP请求的协议版本
   * @param[in] v 协议版本0x11, 0x10
   */
  void setVersion(uint8_t v) { m_version = v;}

  /**
   * @brief 设置HTTP请求的路径
   * @param[in] v 请求路径
   */
  void setPath(const std::string& v) { m_path = v;}

  /**
   * @brief 设置HTTP请求的查询参数
   * @param[in] v 查询参数
   */
  void setQuery(const std::string& v) { m_query = v;}

  /**
   * @brief 设置HTTP请求的Fragment
   * @param[in] v fragment
   */
  void setFragment(const std::string& v) { m_fragment = v;}

  /**
   * @brief 设置HTTP请求的消息体
   * @param[in] v 消息体
   */
  void setBody(const std::string& v) { m_body = v;}

  /**
   * @brief 是否自动关闭
   */
  bool isClose() const { return m_close;}

  /**
   * @brief 设置是否自动关闭
   */
  void setClose(bool v) { m_close = v;}

  /**
   * @brief 是否websocket
   */
  bool isWebsocket() const { return m_websocket;}

  /**
   * @brief 设置是否websocket
   */
  void setWebsocket(bool v) { m_websocket = v;}

  /**
   * @brief 设置HTTP请求的头部MAP
   * @param[in] v map
   */
  void setHeaders(const MapType& v) { m_headers = v;}

  /**
   * @brief 设置HTTP请求的参数MAP
   * @param[in] v map
   */
  void setParams(const MapType& v) { m_params = v;}

  /**
   * @brief 设置HTTP请求的Cookie MAP
   * @param[in] v map
   */
  void setCookies(const MapType& v) { m_cookies = v;}

  //获取HTTP请求的头部参数，如果存在则返回对应值，不存在则返回默认值
  std::string getHeader(const std::string& key, const std::string& def = "") const;
  //获取HTTP请求的请求参数
  std::string getParam(const std::string& key, const std::string& def = "") const;
  //获取HTTP请求的cookie参数
  std::string getCookie(const std::string& key, const std::string& def = "") const;
  //设置HTTP请求的头部参数
  void setHeader(const std::string& key, const std::string& val);
  //设置HTTP请求的请求参数
  void setParam(const std::string& key, const std::string& val);
  //设置HTTP请求的cookie参数
  void setCookie(const std::string& key, const std::string& val);
  //删除HTTP请求的头部参数
  void delHeader(const std::string& key);
  //删除HTTP请求的请求参数
  void delParam(const std::string& key);
  //删除HTTP请求的Cookie参数
  void delCookie(const std::string& key);
  //判断HTTP请求的头部参数是否存在
  bool hasHeader(const std::string& key, std::string* val = nullptr);
  //判断HTTP请求的请求参数是否存在
  bool hasParam(const std::string& key, std::string* val = nullptr);
  //判断HTTP请求的Cookie参数是否存在
  bool hasCookie(const std::string& key, std::string* val = nullptr);

  //检查并获取HTTP请求的头部参数
  template <class T>
  bool checkGetHeaderAs(const std::string& key, T& val, const T& def = T()) {
    return checkGetAs(m_headers, key, val, def);
  }
  //获取HTTP请求的头部参数
  template <class T>
  T getHeaderAs(const std::string& key, const T& def = T()) {
    return getAs(m_headers, key, def);
  }
  //检查并获取HTTP请求的请求参数
  template <class T>
  bool checkGetParamAs(const std::string& key, T& val, const T& def = T()) {
    initQueryParam();
    initBodyParam();
    return checkGetAs(m_params, key, val, def);
  }
  //获取HTTP请求的请求的请求参数
  template <class T>
  T getParamAs(const std::string& key, const T& def = T()) {
    initQueryParam();
    initBodyParam();
    return getAs(m_params, key, def);
  }
  //检查并获取HTTP请求的cookie参数
  template <class T>
  bool checkGetCookieAs(const std::string& key, T& val, const T& def = T()) {
    initCookies();
    return checkGetAs(m_cookies, key, val, def);
  }
  //获取HTTP请求的请求的请求参数
  template <class T>
  T getCookieAs(const std::string& key, const T& def = T()) {
    initCookies();
    return getAs(m_cookies, key, def);
  }

  
  std::string toString() const;
  std::ostream& dump(std::ostream& os) const;

  void init();
  void initParam();
  void initQueryParam();
  void initBodyParam();
  void initCookies();

private:
  //HTTP方法
  HttpMethod m_method;
  //HTTP版本
  uint8_t m_version;
  //是否主动关闭连接，如果为true表示客户端希望服务器响应后关闭连接，为false表示保持连接
  bool m_close;
  //是否为websocket，如果请求头包含Upgrade: websocket，说明这是一个websocket请求
  bool m_websocket;
  //解析参数时的标志位：控制是否已经解析了Query、Body、Cookie等参数，避免重复解析
  bool m_parserParamFlag;

  // GET /search?q=c++&page=1 HTTP/1.1
  //请求的路径部分(不含参数)，来源于请求行的URL   示例中的/search
  std::string m_path;
  //请求url中?后的查询参数,来源于请求行   示例中的q=c++&page=1
  std::string m_query;

  //url的锚点
  std::string m_fragment;
  //请求的消息体,通常出现现在POST PUT方法的HTTP请求中
  std::string m_body;

  //请求头部MAP,对应如下示例部分
  // Host: www.example.com
  // User-Agent: Mozilla/5.0    
  // Accept: text/html  
  MapType m_headers;

  //将query参数(以及body中的url编码参数)解析成键值对存储
  //来源是url中的查询参数(如GET /search?q=c%2B%2B&page=1&sort=desc HTTP/1.1中的q=c%2B%2B&page=1&sort=desc)
  //或body中的username=abc&password=123
  MapType m_params;

  //解析后的cookie键值对
  //Cookie: sessionid=abc123;
  MapType m_cookies;

//各成员与实际请求的对应关系
// +-----------------------------------------------------------+
// |                      HTTP 请求行                           |
// +-----------------------------------------------------------+
// | GET /search?q=c%2B%2B&page=1&sort=desc HTTP/1.1            | 
// +-----------------------------------------------------------+
//       |                         |                     |
//       v                         v                     v
// +------------------+    +-------------------+    +-------------------+
// | m_method        |    | m_path            |    | m_version         |
// | "GET"           |    | "/search"         |    | "1.1"             |
// +------------------+    +-------------------+    +-------------------+
//                               |
//                               v
//                      +-------------------+
//                      | m_query            |
//                      | "q=c%2B%2B&page=1&sort=desc" |
//                      +-------------------+

// +-----------------------------------------------------------+--------------------------+------
// |                      请求头部部分                          |                                 |
// +-----------------------------------------------------------+--------------------------+------
// | Host: www.example.com                                                                       |  
// | User-Agent: Mozilla/5.0                                                                     |
// | Accept: text/html                                                                           |
// | Cookie: sessionid=abc123;                                                                   |
// | Connection: keep-alive                                                                      |
// | Content-Length: 50                                                                          |
// | Upgrade: websocket                                                                          |
// +-----------------------------------------------------------+-------------------------+--------
//       |                                      |                  |                    |
//       v                                      v                  v                    v
// +-------------------+               +------------------+  +-------------+  +------------------+
// | m_headers         |                  | m_cookies|    |      m_close     |  |    m_websocket   |
// | {"Host": "www.example.com"} | {"sessionid": "abc123"} |      false         |       true       |
// | {"User-Agent": "Mozilla/5.0"} |                       |                    |                  |
// | {"Accept": "text/html"}       |                       |                    |                  |
// | {"Connection": "keep-alive"}  |                       |                    |                  |
// | {"Content-Length": "50"}      |                       |                    |                  |
// | {"Upgrade": "websocket"}      |                       |                    |                  |
// +-------------------+  +------------------+  +-------------+  +------------------+

// +-----------------------------------------------------------+
// |                      请求体部分                             |
// +-----------------------------------------------------------+
// | username=abc&password=123                                  |  ← 请求体 (Body)
// +-----------------------------------------------------------+
//       |
//       v
// +-------------------+
// | m_body            |
// | "username=abc&password=123"  |
// +-------------------+
//       |
//       v
// +-------------------+
// | m_params          |
// | {"username": "abc", "password": "123"} |
// +-------------------+

// +-----------------------------------------------------------+
// |                      锚点部分                               |
// +-----------------------------------------------------------+
// | #section1                                               |
// +-----------------------------------------------------------+
//       |
//       v
// +-------------------+
// | m_fragment        |
// | "section1"        |
// +-------------------+

  
};


//HTTP响应结构
class HttpResponse {
public:
    typedef std::shared_ptr<HttpResponse> ptr;
    typedef std::map<std::string, std::string, CaseInsensitiveLess> MapType;

    HttpResponse(uint8_t version = 0x11, bool close = true);
    

    //返回响应状态
    HttpStatus getStatus() const { return m_status;}
    //返回响应版本
    uint8_t getVersion() const { return m_version;}
    //返回响应消息体
    const std::string& getBody() const { return m_body;}
    //返回响应原因
    const std::string& getReason() const { return m_reason;}
    //返回响应头部MAP
    const MapType& getHeaders() const { return m_headers;}

    //设置响应状态
    void setStatus(HttpStatus v) { m_status = v;}
    //设置响应版本
    void setVersion(uint8_t v) { m_version = v;}
    //设置响应消息体
    void setBody(const std::string& v) { m_body = v;}
    //设置响应原因
    void setReason(const std::string& v) { m_reason = v;}
    //设置响应头部MAP
    void setHeaders(const MapType& v) { m_headers = v;}

    //是否自动关闭
    bool isClose() const { return m_close;}
    //设置是否自动关闭
    void setClose(bool v) { m_close = v;}
    //是否websocket
    bool isWebsocket() const { return m_websocket;}
    //设置是否websocket
    void setWebsocket(bool v) { m_websocket = v;}

    //获取响应头部参数
    std::string getHeader(const std::string& key, const std::string& def = "") const;
    //设置响应头部参数
    void setHeader(const std::string& key, const std::string& val);
    //删除响应头部参数
    void delHeader(const std::string& key);

    //检查并获取HTTP响应的头部参数
    template <class T>
    bool checkGetHeaderAs(const std::string& key, T& val, const T& def = T()) {
      return checkGetAs(m_headers, key, val, def);
    }
    //获取HTTP响应的头部参数
    template <class T>
    T getHeaderAs(const std::string& key, const T& def = T()) {
      return getAs(m_headers, key, def);
    }

    std::string toString() const;
    std::ostream& dump(std::ostream& os) const;

    // 它的作用是设置一个 HTTP 重定向响应，也就是告诉客户端：“你请求的资源已经移动到另一个地址了，请去那里访问。”
    void setRedirect(const std::string& url);
    //设置一个http响应的cookie
    //这个函数用于向客户端设置一个 Set-Cookie 响应头，让客户端（浏览器）
    //在后续请求中自动带上这个 Cookie，用于身份标识、会话控制等。
    // 参数说明：
    // key     ：Cookie 名称（键）
    // val     ：Cookie 的值
    // expired ：过期时间，单位是 time_t 类型的时间戳（0 表示临时 Cookie）
    // path    ：Cookie 的作用路径，默认空，通常写为 "/" 表示整站有效
    // domain  ：Cookie 的作用域（域名），如 ".sylar.top"，默认为当前域
    // secure  ：是否仅在 HTTPS 连接中发送该 Cookie，true 表示启用 Secure 属性
    void setCookie(const std::string& key, const std::string& val,
                    time_t expired, const std::string& path, 
                    const std::string& domain, bool secure);


private:
// 响应状态码，例如 200 OK、404 Not Found、500 Internal Server Error 等
    // 对应 HTTP 响应行的状态部分，如 HTTP/1.1 200 OK 中的 200
    HttpStatus m_status;

    // 响应版本号，通常是 0x10 表示 HTTP/1.0，0x11 表示 HTTP/1.1
    // 对应 HTTP 响应行的版本部分，如 HTTP/1.1
    uint8_t m_version;

    // 是否自动关闭连接
    // 如果为 true，表示响应后关闭 TCP 连接；false 则表示 keep-alive（保持连接）
    // 对应响应头中的 Connection 字段
    bool m_close;

    // 是否是 WebSocket 响应
    // 如果响应头中包含 Upgrade: websocket 则为 true，表示这是一次握手响应
    bool m_websocket;

    // 响应体（Body）
    // 主要承载响应内容，如 HTML 页面、JSON 数据等
    // 对应响应中空行之后的主体部分
    std::string m_body;

    // 响应原因短语
    // 通常与状态码一起出现，如 "OK", "Not Found"，用于人类阅读
    // 对应 HTTP 响应行中的原因短语部分，如 HTTP/1.1 404 Not Found 中的 Not Found
    std::string m_reason;

    // 响应头部字段 Map
    // 保存所有响应头，例如 Content-Type、Server、Set-Cookie 等
    // 格式如：{ "Content-Type": "text/html", "Server": "MyServer/1.0" }
    MapType m_headers;

    // Set-Cookie 头中设置的 Cookie 字符串集合
    // 每个元素对应一个完整的 Set-Cookie 头字段内容，未解析成键值对
    // 如：Set-Cookie: sessionid=abc123; Path=/; HttpOnly
    std::vector<std::string> m_cookies;

// +------------------------------------------------------------+
// |                  HTTP 响应行（Response Line）               |
// +------------------------------------------------------------+
// | HTTP/1.1 200 OK                                             | ← m_version / m_status / m_reason
// +------------------------------------------------------------+

// +------------------------------------------------------------+
// |                  响应头部字段（Headers）                    |
// +------------------------------------------------------------+
// | Content-Type: text/html                                     |
// | Connection: close                                           |
// | Set-Cookie: sessionid=abc123; Path=/; HttpOnly             | ← m_cookies
// | Server: MyServer/1.0                                       | ← m_headers
// +------------------------------------------------------------+

// +------------------------------------------------------------+
// |                        空行（CRLF）                         |
// +------------------------------------------------------------+

// +------------------------------------------------------------+
// |                      响应体（Body）                         |
// +------------------------------------------------------------+
// | <html>...</html>                                           | ← m_body
// +------------------------------------------------------------+

};


//流式输出两个类
std::ostream& operator<<(std::ostream& os, const HttpRequest& req);
std::ostream& operator<<(std::ostream& os, const HttpResponse& req);

}
}

#endif