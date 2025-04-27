#include "http.h"
#include "sylar/util.h"

#include <string>

namespace sylar {
namespace http {

HttpMethod StringToHttpMethod(const std::string& m) {
#define XX(num, name, string)   \
    if (strcmp(#string, m.c_str()) == 0) {  \
        return HttpMethod::name;            \
    }
    HTTP_METHOD_MAP(XX)
#undef XX
    return HttpMethod::INVALID_METHOD;
}

HttpMethod CharsToHttpMethod(const char* m) {
#define XX(num, name, string) \
    if (strncmp(#string, m, strlen(#string)) == 0) { \
        return HttpMethod::name;                    \
    }

    HTTP_METHOD_MAP(XX);
#undef XX
    return HttpMethod::INVALID_METHOD;
}

static const char* s_method_string[] = {
#define XX(num, name, string) #string, 
    HTTP_METHOD_MAP(XX)           
#undef XX
};

const char* HttpMethodToString(const HttpMethod& m) {
    uint32_t idx = (uint32_t)m;
    if (idx >= (sizeof(s_method_string) / sizeof(s_method_string[0]))) {
        return "<unknown>";
    }
    return s_method_string[idx];
}

//在宏中 # 是字符串化运算符，将参数转化成一个字符串字面量
const char* HttpStatusToString(const HttpStatus& s) {
    switch(s) {
#define XX(code, name, desc) \
        case HttpStatus::name: \
            return #desc;       
        HTTP_STATUS_MAP(XX);
#undef XX
        default :
            return "<unknown>";
    }
}

bool CaseInsensitiveLess::operator()(const std::string& lhs, const std::string& rhs) const {
    //返回值<0：lhs < rhs 
    //返回值>0：lhs > rhs 
    //返回值=0：lhs = rhs 
    return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
}


//HttpRequest
HttpRequest::HttpRequest(uint8_t version, bool close) 
    : m_method(HttpMethod::GET),
      m_version(version),
      m_close(false),
      m_websocket(false),
      m_parserParamFlag(0),
      m_path("/"){
}

std::shared_ptr<HttpResponse> HttpRequest::createResponse() {
    std::shared_ptr<HttpResponse> rsp(new HttpResponse(getVersion(), isClose()));
    return rsp;
}

std::string HttpRequest::getHeader(const std::string& key, const std::string& def = "") const {
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

std::string HttpRequest::getParam(const std::string& key, const std::string& def = "") const {
    auto it = m_params.find(key);
    return it == m_params.end() ? def : it->second;
}

std::string HttpRequest::getCookie(const std::string& key, const std::string& def = "") const {
    auto it = m_cookies.find(key);
    return it == m_cookies.end() ? def : it->second;
}

void HttpRequest::setHeader(const std::string& key, const std::string& val) {
    m_headers[key] = val;
}

void HttpRequest::setParam(const std::string& key, const std::string& val) {
    m_params[key] = val;
}

void HttpRequest::setCookie(const std::string& key, const std::string& val) {
    m_cookies[key] = val;
}

void HttpRequest::delHeader(const std::string& key) {
    m_headers.erase(key);
}

void HttpRequest::delParam(const std::string& key) {
    m_params.erase(key);
}

void HttpRequest::delCookie(const std::string& key) {
    m_cookies.erase(key);
}


bool HttpRequest::hasHeader(const std::string& key, std::string* val) {
    auto it = m_headers.find(key);
    if(it == m_headers.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasParam(const std::string& key, std::string* val) {
    initQueryParam();
    initBodyParam();
    auto it = m_params.find(key);
    if(it == m_params.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasCookie(const std::string& key, std::string* val) {
    initCookies();
    auto it = m_cookies.find(key);
    if(it == m_cookies.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

std::string HttpRequest::toString() const {
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

std::ostream& HttpRequest::dump(std::ostream& os) const {
    // HttpMethod m_method = HttpMethod::GET;    // HTTP 方法：GET
    // std::string m_path = "/search";           // 请求路径：/search
    // std::string m_query = "q=c++&page=1";     // 查询参数：q=c++&page=1
    // std::string m_fragment = "section1";      // 锚点部分：section1
    // uint8_t m_version = 0x11;                 // HTTP/1.1
    // bool m_websocket = false;                 // 非 WebSocket 请求
    // bool m_close = false;                     // 保持连接
    // MapType m_headers = {                     // 请求头部
    //     {"Host", "www.sylar.top"},
    //     {"User-Agent", "Mozilla/5.0"},
    //     {"Accept", "text/html"}
    // };
    // std::string m_body = "username=abc&password=123";  // 请求体
    //                    |
    //                    |
    //                    |
    //                    V
    // GET /search?q=c++&page=1#section1 HTTP/1.1
    // connection: keep-alive
    // Host: www.sylar.top
    // User-Agent: Mozilla/5.0
    // Accept: text/html
    //
    // content-length: 27  请求体的大小
    // username=abc&password=123
    os << HttpMethodToString(m_method) << " "
       << m_path
       << (m_query.empty() ? "" : "?")
       << m_query
       << (m_fragment.empty() ? "" : "#")
       << m_fragment
       << " HTTP/"
       << ((uint32_t)(m_version >> 4))
       << "."
       << ((uint32_t)(m_version & 0x0F))
       << "\r\n";
    if(!m_websocket) {
        os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }
    for(auto& i : m_headers) {
        if(!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }

    if(!m_body.empty()) {
        os << "content-length: " << m_body.size() << "\r\n\r\n"
           << m_body;
    } else {
        os << "\r\n";
    }
    return os;
}

void HttpRequest::init() {
    std::string conn = getHeader("connection");
    if (!conn.empty()) {
        if (strcmp(conn.c_str(), "keep-alive") == 0) {
            m_close = false;
        } else {
            m_close = true;
        }
    }
}

void HttpRequest::initParam() {
    initQueryParam();
    initBodyParam();
    initCookies();
}

//去掉字符串头部和尾部多余的空格
std::string trim(const const std::string& str) {
    size_t start = 0;
    size_t end = str.size() - 1;
    while (start <= end && (str[start] == ' ')) {
        start++;
    }
    if (start == (end + 1)) {
        return "";
    }
    while (end > start && (str[end] == ' ')) {
        end--;
    }
    return str.substr(start, end - start + 1);
}

void HttpRequest::initQueryParam() {
    //每个位作为某个东西是否初始化的标志位，按位去判断的好处是节省了内存大小
    if (m_parserParamFlag & 0x01) {
        return;
    }
//遍历字符串str(假设他是一个URL参数，例如a=1&b=2&c=3)，按照给定的flag(这里是&)分割键值对
//每个键值对用=分割键和值，把解码后的结果放入m(map/unordered_map)中
#define PARSE_PARAM(str, m, flag, trim)                                           \
    /* 初始化位置索引为0，用于从头开始解析字符串 */                                   \
    size_t pos = 0;                                                                \
    do {                                                                           \
        /* 记录当前字段的起始位置，用于后续截取 key */                                 \
        size_t last = pos;                                                          \
        /* 查找等号的位置，用于分隔 key 和 value */                                    \
        pos = str.find('=', pos);                                                     \
        /* 如果找不到等号，说明没有更多的参数对了，退出循环 */                              \
        if (pos == std::string::npos) {                                                  \
            break;                                                                        \
        }                                                                           \
        /* 保存等号位置，用于后续截取 key 和 value */                                   \
        size_t key = pos;                                                             \
        /* 查找参数之间的分隔符（如 &），用于确定 value 的结束位置 */                       \
        pos = str.find(flag, pos);                                                      \
        /* 向 map 插入解析出的 key-value 对，key 去除空白，value 做 URL 解码 */             \
        m.insert(std::make_pair(                                                          \
            trim(str.substr(last, key - last)),                                            \
            sylar::StringUtil::UrlDecode(str.substr(key + 1, pos - key - 1))));             \
        /* 如果找不到分隔符，说明这是最后一个参数，结束循环 */                                   \
        if (pos == std::string::npos) {                                             \
            break;                                                                   \
        }                                                                             \
        /* 跳过分隔符，准备解析下一个参数对 */                                            \
        ++pos;                                                                          \
    /* 循环解析直到所有 key-value 对都处理完 */                                            \
    } while (true);                                                                      
                                                             
    PARSE_PARAM(m_query, m_params, '&', sylar::http::trim)  
    m_parserParamFlag |= 0x01;                                            
}

void HttpRequest::initBodyParam() {
    if (m_parserParamFlag & 0x2) {
        return;
    }
    std::string content_type = getHeader("content_type");
    if (strcasestr(content_type.c_str(), "application/x-www-urlencoded") == nullptr) {
        //strcasestr(a, b)的作用是在字符串a中看是否能找到子串b，如果能找到返回b在a中第一次出现的位置，找不到返回nullptr
        //如果找不到的话，说明了请求体的格式不是a=1&b=2的格式，如果不是这种合适就没法解析
        //content_type还可以是application/json表示请求体是json格式，不能用key-value解析
        //content_type还可以是multipart/from-data表示文件上传，不能用key-value解析
        m_parserParamFlag |= 0x2;
        return;
    }
    PARSE_PARAM(m_body, m_params, '&', sylar::http::trim);
    m_parserParamFlag |= 0x2;

}

void HttpRequest::initCookies() {
    if (m_parserParamFlag & 0x4) return;
    std::string cookie = getHeader("cookie");
    if (cookie.empty()) {
        m_parserParamFlag |= 0x4;
        return;
    }
    PARSE_PARAM(cookie, m_cookies, ':', sylar::StringUtil::Trim);
    m_parserParamFlag |= 0x4;
}


//HTTP Response
HttpResponse::HttpResponse(uint8_t version, bool close) 
    : m_status(HttpStatus::OK),
      m_version(version),
      m_close(close),
      m_websocket(false){
}


std::string HttpResponse::getHeader(const std::string& key, const std::string& def = "") const {
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

void HttpResponse::setHeader(const std::string& key, const std::string& val) {
    m_headers[key] = val;
}

void HttpResponse::delHeader(const std::string& key) {
    m_headers.erase(key);
}

std::string HttpResponse::toString() const{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

std::ostream& HttpResponse::dump(std::ostream& os) const {
    // 示例成员值：
    // m_version   = 0x11;                // 表示 HTTP/1.1（高4位主版本号，低4位次版本号）
    // m_status    = HttpStatus::OK;      // 状态码 200
    // m_reason    = "";                  // 原因短语，空则使用默认（OK）
    // m_headers   = {
    //     {"Server", "sylar/1.0"},
    //     {"Content-Type", "text/html"},
    //     {"Connection", "keep-alive"}   // 非 websocket 时将被排除
    // };
    // m_cookies   = {
    //     "uid=abc123; Path=/; HttpOnly",
    //     "token=xyz789; Path=/; Secure"
    // };
    // m_websocket = false;               // 普通 HTTP 响应
    // m_close     = false;               // 连接保持
    // m_body      = "<html>Hello</html>";  // 响应正文

    // 构建响应行，如：
    // HTTP/1.1 200 OK
    os << "HTTP/"
       << ((uint32_t)(m_version >> 4))         // 主版本号（如 1）
       << "."
       << ((uint32_t)(m_version & 0x0F))       // 次版本号（如 1）
       << " "
       << (uint32_t)m_status                   // 状态码，如 200
       << " "
       << (m_reason.empty() 
           ? HttpStatusToString(m_status)      // 若 reason 为空，则根据状态码自动填充 "OK"
           : m_reason)                         // 否则输出指定原因短语
       << "\r\n";
    // 输出所有 headers
    for(auto& i : m_headers) {
        // 若非 WebSocket 且是 "Connection" 头，由系统统一输出，跳过用户设置的
        if(!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";  // 输出头部，如：Server: sylar/1.0
    }
    // 输出所有 cookies（每个以 Set-Cookie 开头）
    for(auto& i : m_cookies) {
        os << "Set-Cookie: " << i << "\r\n";  // 如：Set-Cookie: uid=abc123; Path=/; HttpOnly
    }
    // 非 WebSocket 时手动输出 connection 头
    if(!m_websocket) {
        os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }
    // 如果响应体不为空，输出 content-length 和内容
    if(!m_body.empty()) {
        os << "content-length: " << m_body.size() << "\r\n\r\n"  // 空行后输出正文
           << m_body;
    } else {
        os << "\r\n"; // 没有正文，仅输出头部结束空行
    }
    return os;
}

void HttpResponse::setRedirect(const std::string& url) {
    //具体发送给客户端的样子可能如下：
    //HTTP/1.1 302 Found
    //Location: https://www.example.com/login
    //Content-Length: 0
    //Connection: close
    //客户端(浏览器)接收到这个响应后就会跳转到url
    m_status = HttpStatus::FOUND;
    setHeader("location", url);
}

void HttpResponse::setCookie(const std::string& key, const std::string& val,
    time_t expired, const std::string& path, 
    const std::string& domain, bool secure) {
    std::stringstream ss;
    ss << key << "=" << val;
    if (expired > 0) {
        ss << ";expires=" << sylar::Time2Str(expired, "%a, %d %b %Y %H:%M:%S") << " GMT";
    }
    if (!domain.empty()) {
        ss << ";domain=" << domain; 
    }
    if (!path.empty()) {
        ss << ";path=" << path;
    }
    if (secure) {
        ss << ";secure";
    }
    m_cookies.push_back(ss.str());
}

std::ostream& operator<<(std::ostream& os, const HttpRequest& req) {
    return req.dump(os);
}

std::ostream& operator<<(std::ostream& os, const HttpResponse& rsp) {
    return rsp.dump(os);
}

}
}