#include "http_parser.h"
#include "config.h"
#include "log.h"

namespace sylar {
namespace http {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

//定义配置项
static sylar::ConfigVar<uint64_t>::ptr g_http_request_buffer_size = 
                            sylar::Config::Lookup("http.request.buffer_size",
                                                (uint64_t)(4 * 1024), 
                                                "http request buffer size");
static sylar::ConfigVar<uint64_t>::ptr g_http_request_max_body_size = 
                            sylar::Config::Lookup("http.request.max_body_size",
                                                (uint64_t)(64 * 1024  * 1024), 
                                                "http request max body size");
static sylar::ConfigVar<uint64_t>::ptr g_http_response_buffer_size = 
                            sylar::Config::Lookup("http.response.buffer_size",
                                                (uint64_t)(4 * 1024), 
                                                "http response buffer size");
static sylar::ConfigVar<uint64_t>::ptr g_http_response_max_body_size = 
                            sylar::Config::Lookup("http.response.max_body_size",
                                                (uint64_t)(64 * 1024  * 1024), 
                                                "http response max body size");

static uint64_t s_http_request_buffer_size = 0;
static uint64_t s_http_request_max_body_size = 0;
static uint64_t s_http_response_buffer_size = 0;
static uint64_t s_http_response_max_body_size = 0;

uint64_t HttpRequestParser::GetHttpRequestBufferSize() {
    return s_http_request_buffer_size;
}
uint64_t HttpRequestParser::GetHttpRequestMaxBodySize() {
    return s_http_request_max_body_size;
}
uint64_t HttpResponseParser::GetHttpRequestBufferSize() {
    return s_http_response_buffer_size;
}
uint64_t HttpResponseParser::GetHttpRequestMaxBodySize() {
    return s_http_response_max_body_size;
}

//在程序启动时
//读取配置项的当前值，赋值给缓存变量s_http_xx
//注册监听器，当配执项的值发生改变时，自动更新缓存变量
namespace {
struct _SizeIniter {
    _SizeIniter() {
        s_http_request_buffer_size = g_http_request_buffer_size->getValue();
        s_http_request_max_body_size = g_http_request_max_body_size->getValue();
        s_http_response_buffer_size = g_http_response_buffer_size->getValue();
        s_http_response_max_body_size = g_http_response_max_body_size->getValue();

        g_http_request_buffer_size->addListener([](const uint64_t& old_value, const uint64_t& new_value){
            s_http_request_buffer_size = new_value;
        });
        g_http_request_max_body_size->addListener([](const uint64_t& old_value, const uint64_t& new_value){
            s_http_request_max_body_size = new_value;
        });
        g_http_response_buffer_size->addListener([](const uint64_t& old_value, const uint64_t& new_value){
            s_http_response_buffer_size = new_value;
        });
        g_http_response_max_body_size->addListener([](const uint64_t& old_value, const uint64_t& new_value){
            s_http_response_max_body_size = new_value;
        });

    }
};
}


//这个方法是HTTP请求解析过程中,解析请求方法(POST GET等)的具体回调函数
//当HTTP解析器http_parase在解析到“请求方法”字段时自动调用这个回调函数，我们会在这个回调函数中设置解析得到的方法
//data：要设置的类，这里是HttpRequestParser，要设置这个类的m_data成员，m_data是解析结果存入的地方，即HttpRequest类
//at：指向“方法字符串”的起始指针，如指向GET的G
//length：方法字符串的长度，比如GET的3
void on_request_method(void* data, const char* at, size_t length) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    //将解析出来的字符串准为枚举值HttpMethod
    HttpMethod m = CharsToHttpMethod(at);
    if (m == HttpMethod::INVALID_METHOD) {
        parser->setError(1000);
        return;
    }
    parser->getData()->setMethod(m);
}
void on_request_uri(void* data, const char* at, size_t length) {
}
void on_request_fragment(void* data, const char* at, size_t length) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setFragment(std::string(at, length));
}
void on_request_path(void* data, const char* at, size_t length) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setPath(std::string(at, length));
}
void on_request_query(void* data, const char* at, size_t length) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setQuery(std::string(at, length));
}
void on_request_version(void* data, const char* at, size_t length) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    uint8_t v;
    if (strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if (strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        SYLAR_LOG_WARN(g_logger) << "invalid http request version " << std::string(at, length);
        parser->setError(1001);
        return; 
    }
    parser->getData()->setVersion(v);
}
//这个回调函数解析的是请求头这部分，http_parser每解析一行请求头时都会调用一次这个函数
//Host: www.example.com                                                                       |  
//User-Agent: Mozilla/5.0                                                                     |
//Accept: text/html                                                                           |
//Cookie: sessionid=abc123; 
//field:字段名，如Host  flen：字段名的长度
//value:字段值，如www.example.com   vlen：字段值的长度
void on_request_http_field(void* data, const char* field, size_t flen, const char* value, size_t vlen) {
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    if (flen == 0) return;
    parser->getData()->setHeader(std::string(field, flen), std::string(value, vlen));
}
//该回调函数的作用是，当所有的请求头header都解析完了后，会执行这个回调函数
void on_request_header_done(void* data, const char* at, size_t length) {
}

HttpRequestParser::HttpRequestParser()
    : m_error(0) {
    m_data.reset(new sylar::http::HttpRequest);
    http_parser_init(&m_parser);
    m_parser.request_method = on_request_method;
    m_parser.request_uri = on_request_uri;
    m_parser.fragment = on_request_fragment;
    m_parser.request_path = on_request_path;
    m_parser.query_string = on_request_query;
    m_parser.http_version = on_request_version;
    m_parser.header_done = on_request_header_done;
    m_parser.http_field = on_request_http_field;
    m_parser.data = this;
}

uint64_t HttpRequestParser::getContentLength() {
    return m_data->getHeaderAs<uint64_t>("content-length", 0);
}

//返回1表示成功
//返回-1表示错误
//返回>0表示成功处理的字节数
size_t HttpRequestParser::execute(char* data, size_t len) {
    size_t offset = http_parser_execute(&m_parser, data, len, 0);
    //memmove和mepcpy都是用来复制内存的函数，把一块内存的数据拷贝到另一块内存区域
    //区别在于memmove内层做了判断，如果拷贝到内存有重叠，会从后往前复制，避免数据被覆盖
    //而memcpy设计的区域如果有重叠，则会直接覆盖，这样可能会导致一些问题
    memmove(data, data + offset, (len - offset));
    return offset;
}

int HttpRequestParser::isFinshed() {
    return http_parser_finish(&m_parser);
}

int HttpRequestParser::hasError() {
    return m_error || http_parser_has_error(&m_parser);
}

void on_response_reason(void* data, const char* at, size_t length) {
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    parser->getData()->setReason(std::string(at, length));
}

void on_response_status(void* data, const char* at, size_t length) {
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    HttpStatus status = (HttpStatus)(atoi(at));
    parser->getData()->setStatus(status);
}

void on_response_chunk(void* data, const char* at, size_t length) {
}

void on_response_version(void* data, const char* at, size_t length) {
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    uint8_t v;
    if (strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if (strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        SYLAR_LOG_WARN(g_logger) << "invalid http response version " << std::string(at, length);
        parser->setError(1001);
        return; 
    }
    parser->getData()->setVersion(v);
}
void on_response_last_chunk(void* data, const char* at, size_t length) {
}
void on_response_header_done(void* data, const char* at, size_t length){
}
void on_response_http_field(void* data, const char* field, size_t flen, const char* value, size_t vlen) {
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    if (flen == 0) return;
    parser->getData()->setHeader(std::string(field, flen), std::string(value, vlen));
}

HttpResponseParser::HttpResponseParser()
    :m_error(0) {
    m_data.reset(new sylar::http::HttpResponse);
    httpclient_parser_init(&m_parser);
    m_parser.reason_phrase = on_response_reason;
    m_parser.status_code = on_response_status;
    m_parser.chunk_size = on_response_chunk;
    m_parser.http_version = on_response_version;
    m_parser.header_done = on_response_header_done;
    m_parser.last_chunk = on_response_last_chunk;
    m_parser.http_field = on_response_http_field;
    m_parser.data = this;
}

size_t HttpResponseParser::execute(char* data, size_t len, bool chunk) {
    if (chunk) {
        httpclient_parser_init(&m_parser);
    }
    size_t offset = httpclient_parser_execute(&m_parser, data, len, 0);
    memmove(data, data + offset, (len - offset));
    return offset;
}

int HttpResponseParser::isFinshed() {
    return httpclient_parser_finish(&m_parser);
}

int HttpResponseParser::hasError() {
    return m_error || httpclient_parser_has_error(&m_parser);
}

uint64_t HttpResponseParser::getContentLength() {
    return m_data->getHeaderAs<uint64_t>("content-length", 0);
}

}
}