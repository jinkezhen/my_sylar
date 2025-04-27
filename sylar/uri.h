/**
 * @file uril.h
 * @brief URI封装类
 * @date 2025-04-26
 * @copyright Copyright (2025) All rights reserved
 */

//该文件的作用是定义一个类，用于解析、封装和生成URI(统一资源标识符)，
//比如foo://user@sylar.com:8042/over/there?name=ferret#nose
//核心作用是将这种字符串格式的URI解析为结构化的信息，便于程序内部使用和操作，
//同时还可以重新序列化为字符串

#include <memory>
#include <string>
#include <stdint.h>
#include "address.h"

#ifndef __SYLAR_URI_H__
#define __SYLAR_URI_H__

namespace sylar {

//     foo://user@sylar.com:8042/over/there?name=ferret#nose
//     \_/   \______________/\_________/ \_________/ \__/
//      |           |            |            |        |
//   协议        权限部分         路径          查询      片段标识
//  (scheme)   (authority)       (path)       (query)   (fragment)

// 详细说明：
// - scheme（协议）：foo → 表示使用的协议，如 http、ftp、自定义协议等
// - authority（权限部分）：user@sylar.com:8042 → 包含用户信息、主机名和端口号
//  - user → 用户名（可选）
//  - sylar.com → 主机名（域名或 IP）
//  - 8042 → 端口号（可选，若未提供则使用默认端口）
// - path（路径）：/over/there → 指定服务器上的资源路径
// - query（查询参数）：name=ferret → 提供参数信息，通常用于 GET 请求参数
// - fragment（片段标识）：nose → 指向资源内的某个位置，如网页锚点

class Uri {
public:
    typedef std::shared_ptr<Uri> ptr;

    static Uri::ptr Create(const std::string& uri);
    Uri();

    /**
     * @brief 返回scheme
     */
    const std::string& getScheme() const { return m_scheme;}

    /**
     * @brief 返回用户信息
     */
    const std::string& getUserinfo() const { return m_userinfo;}

    /**
     * @brief 返回host
     */
    const std::string& getHost() const { return m_host;}

    /**
     * @brief 返回路径
     */
    const std::string& getPath() const;

    /**
     * @brief 返回查询条件
     */
    const std::string& getQuery() const { return m_query;}

    /**
     * @brief 返回fragment
     */
    const std::string& getFragment() const { return m_fragment;}

    /**
     * @brief 返回端口
     */
    int32_t getPort() const;

    /**
     * @brief 设置scheme
     * @param v scheme
     */
    void setScheme(const std::string& v) { m_scheme = v;}

    /**
     * @brief 设置用户信息
     * @param v 用户信息
     */
    void setUserinfo(const std::string& v) { m_userinfo = v;}

    /**
     * @brief 设置host信息
     * @param v host
     */
    void setHost(const std::string& v) { m_host = v;}

    /**
     * @brief 设置路径
     * @param v 路径
     */
    void setPath(const std::string& v) { m_path = v;}

    /**
     * @brief 设置查询条件
     * @param v
     */
    void setQuery(const std::string& v) { m_query = v;}

    /**
     * @brief 设置fragment
     * @param v fragment
     */
    void setFragment(const std::string& v) { m_fragment = v;}

    /**
     * @brief 设置端口号
     * @param v 端口
     */
    void setPort(int32_t v) { m_port = v;}

    /**
     * @brief 序列化到输出流
     * @param os 输出流
     * @return 输出流
     */
    std::ostream& dump(std::ostream& os) const;

    /**
     * @brief 转成字符串
     */
    std::string toString() const;

    /**
     * @brief 获取Address
     */
    Address::ptr createAddress() const;

private:
    // 每种协议（scheme）默认使用某个特定的端口。例如：
    // 协议（scheme）	默认端口（port）
    //    http	            80
    //    https	           443
    //  ws（WebSocket）  	80
    // wss（加密WebSocket）	443
    bool isDefaultPort() const;

private:
    //协议
    std::string m_scheme;
    //用户信息
    std::string m_userinfo;
    //host
    std::string m_host;
    //路径
    std::string m_path;
    //查询参数
    std::string m_query;
    //fragment
    std::string m_fragment;
    //端口
    int32_t m_port;
};

}

#endif