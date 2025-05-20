#include "uri.h"
#include <sstream>


namespace sylar {
%%{
    # ========================
    # URI 解析器状态机定义
    # ========================
    #
    # 本状态机基于 RFC 3986 标准定义 URI 的各组成部分，包括：
    # scheme、authority（包含 userinfo、host、port）、path、query 和 fragment
    #
    # 所有字符类、语法规则及动作均定义在 Ragel 状态机语法中

    machine uri_parser;     # 定义状态机名称为 uri_parser，供 Ragel 编译使用

    # ------------
    # 字符分类定义
    # ------------
    gen_delims = ":" | "/" | "?" | "#" | "[" | "]" | "@";
    # 通用分隔符：这些符号用于划分 URI 的不同部分

    sub_delims = "!" | "$" | "&" | "'" | "(" | ")" | "*" | "+" | "," | ";" | "=";
    # 子分隔符：用于增强语义，但也具有结构作用

    reserved = gen_delims | sub_delims;
    # 保留字符：包括通用分隔符和子分隔符，具有特殊语义

    unreserved = alpha | digit | "-" | "." | "_" | "~";
    # 非保留字符：用于安全传输的普通字符

    pct_encoded = "%" xdigit xdigit;
    # 百分号编码字符：用于编码非 ASCII 字符或保留字符，如 %20 表示空格

    # ------------
    # 动作定义
    # ------------

    action marku { mark = fpc; }
    # 标记当前字符位置（如用于开始记录 URI 的某部分）

    action markh { mark = fpc; }
    # 与 marku 作用相同，但一般用于 host、port 等区域标记

    action save_scheme {
        uri->setScheme(std::string(mark, fpc - mark));
        mark = NULL;
    }
    # 保存 scheme（如 http、https、ftp 等）到 uri 对象中

    scheme = (alpha (alpha | digit | "+" | "-" | ".")*) >marku %save_scheme;
    # scheme 的语法定义：以字母开头，后接字母、数字或 +.- 字符
    # 使用 marku 标记开头位置，%save_scheme 在匹配完成后调用保存函数

    action save_port {
        if (fpc != mark) {
            uri->setPort(atoi(mark));
        }
        mark = NULL;
    }
    # 保存端口号（port）到 uri 对象中
    # 使用 atoi 将字符数组转换为整数

    action save_userinfo {
        if(mark) {
            uri->setUserinfo(std::string(mark, fpc - mark));
        }
        mark = NULL;
    }
    # 保存用户信息（如 user:pass）到 uri 对象中

    action save_host {
        if (mark != NULL) {
            uri->setHost(std::string(mark, fpc - mark));
        }
    }
    # 保存主机名（host），如 domain.com 或 127.0.0.1

    userinfo = (unreserved | pct_encoded | sub_delims | ":")*;
    # userinfo 字符集支持用户名和密码的合法字符

    dec_octet = digit | [1-9] digit | "1" digit{2} | 2 [0-4] digit | "25" [0-5];
    # IPv4 每一段的十进制定义：0-255

    IPv4address = dec_octet "." dec_octet "." dec_octet "." dec_octet;
    # IPv4 地址结构：4 段 dec_octet，用 "." 分隔

    h16 = xdigit{1,4};
    # h16：IPv6 中的一段，1 到 4 个十六进制数字

    ls32 = (h16 ":" h16) | IPv4address;
    # ls32 是 IPv6 最后 32 位部分，可为两段 h16 或 IPv4 格式

    IPv6address = (
                    (h16 ":"){6} ls32 |
                    "::" (h16 ":"){5} ls32 |
                    (h16)? "::" (h16 ":"){4} ls32 |
                    ((h16 ":"){1} h16)? "::" (h16 ":"){3} ls32 |
                    ((h16 ":"){2} h16)? "::" (h16 ":"){2} ls32 |
                    ((h16 ":"){3} h16)? "::" (h16 ":"){1} ls32 |
                    ((h16 ":"){4} h16)? "::" ls32 |
                    ((h16 ":"){5} h16)? "::" h16 |
                    ((h16 ":"){6} h16)? "::"
                 );
    # 完整 IPv6 地址的 9 种合法形式
    # 使用 :: 表示省略 0 字段

    IPvFuture = "v" xdigit+ "." (unreserved | sub_delims | ":")+;
    # IPvFuture：新版本 IPv6 地址，如 v1.fe80::123

    IP_literal = "[" (IPv6address | IPvFuture) "]";
    # 字面 IP 包裹在中括号内（主要是 IPv6）

    reg_name = (unreserved | pct_encoded | sub_delims)*;
    # 注册名称，即非 IP 主机名的合法字符集合

    host = IP_literal | IPv4address | reg_name;
    # host：可以是 IP（v4/v6）或域名字符串

    port = digit*;
    # 端口号为 0 个或多个数字

    authority = (
        (userinfo %save_userinfo "@")?   # 可选用户信息 + @
        host >markh %save_host            # host，保存位置
        (":" port >markh %save_port)?   # 可选端口
    ) >markh;
    # authority 结构体： [userinfo@]host[:port]，对应 URI 中 // 后部分

    action save_segment { mark = NULL; }
    # 标记 path 段后清除 mark（此处未实际保存每一段）

    action save_path {
        uri->setPath(std::string(mark, fpc - mark));
        mark = NULL;
    }
    # 保存 path 字符串，如 /index.html、/search/query

    # pchar 定义路径允许的字符（支持中文）
    pchar = ( (any -- ascii) | unreserved | pct_encoded | sub_delims | ":" | "@" );
    # 除 ASCII 外还包含 unicode 字符，支持中文 URI

    segment = pchar*;
    segment_nz = pchar+;
    segment_nz_nc = (pchar - ":")+;  # 不包含冒号的段

    action clear_segments {}
    # 未使用，可用于清除 path 段集合（若将 path 拆为多个段时）

    path_abempty = ( ("/" segment))? ("/" segment)*;
    path_absolute = ("/" (segment_nz ("/" segment)*)?);
    path_noscheme = segment_nz_nc ("/" segment)*;
    path_rootless = segment_nz ("/" segment)*;
    path_empty = "";
    path = (path_abempty | path_absolute | path_noscheme | path_rootless | path_empty);
    # 各种路径模式的组合（见 RFC 3986），统一归入 path

    action save_query {
        uri->setQuery(std::string(mark, fpc - mark));
        mark = NULL;
    }
    # 保存 ? 后 query 部分

    action save_fragment {
        uri->setFragment(std::string(mark, fpc - mark));
        mark = NULL;
    }
    # 保存 # 后 fragment 部分

    query = (pchar | "/" | "?")* >marku %save_query;
    fragment = (pchar | "/" | "?")* >marku %save_fragment;
    # query 和 fragment 可包含 pchar、/ 和 ? 字符

    hier_part = ("//" authority path_abempty > markh %save_path) | path_absolute | path_rootless | path_empty;
    # 解析层级部分，可能是 authority 开头，也可能直接是 path

    relative_part = ("//" authority path_abempty) | path_absolute | path_noscheme | path_empty;
    relative_ref = relative_part ( "?" query )? ( "#" fragment )?;
    # 相对 URI：无 scheme，以 path 或 authority 开始

    absolute_URI = scheme ":" hier_part ( "?" query )? ;
    relative_URI = relative_part ( "?" query )?;
    # absolute_URI 带 scheme，relative_URI 不带

    URI = scheme ":" hier_part ( "?" query )? ( "#" fragment )?;
    URI_reference = URI | relative_ref;
    # URI_reference 统一包含绝对或相对 URI

    main := URI_reference;
    # 定义主入口状态机为 URI_reference

    write data;
    # 生成数据驱动状态机（data 是 Ragel 的默认处理流）
}%

    Uri::ptr Uri::Create(const std::string& uristr) {
        Uri::ptr uri(new Uri);
        int cs = 0;
        const char* mark = 0;
        %% write init;
        const char *p = uristr.c_str();
        const char *pe = p + uristr.size();
        const char* eof = pe;
        %% write exec;
        if(cs == uri_parser_error) {
            return nullptr;
        } else if(cs >= uri_parser_first_final) {
            return uri;
        }
        return nullptr;
    }
    
    Uri::Uri() : m_port(0) {}
    
    bool Uri::isDefaultPort() const {
        if (m_port == 0) return true;
        if (m_scheme == "http" || m_scheme == "ws) {
            return m_port == 80;
        } else if (m_scheme == "https" || m_scheme == "wss") {
            return m_port == 443;
        }
        return false;
    }

    int32_t Uri::getPort() const {
        if (m_port == 0) return m_port;
        if (m_scheme == "http" || m_scheme == "ws) {
            return 80;
        } else if (m_scheme == "https" || m_scheme == "wss") {
            return 443;
        }
        return m_port;
    }

    const std::string& Uri::getPath() const {
        static std::string s_default_path = "/";
        return m_path.empty() ? s_default_path : m_path;
    }

    std::ostream& Uri::dump(std::ostream& os) const {
        os << m_scheme << "://"
       << m_userinfo
       << (m_userinfo.empty() ? "" : "@")
       << m_host
       << (isDefaultPort() ? "" : ":" + std::to_string(m_port))
       << getPath()
       << (m_query.empty() ? "" : "?")
       << m_query
       << (m_fragment.empty() ? "" : "#")
       << m_fragment;
        return os;
    }

    std::string Uri::dump(std::ostream& os) const {
        std::stringstream ss;
        dump(ss);
        return ss.str();
    }

    Address::ptr Uri::createAddress() const {
        auto addr = Address::LookUpAnyIPAddress(m_host);
        if (addr) {
            addr->setPort(getPort());
        }
        return addr;
    }
}