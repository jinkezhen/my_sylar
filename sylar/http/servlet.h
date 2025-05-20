/**
 * @file servlet.h
 * @brief Servlet封装
 * @date 2025-04-23
 * @copyright Copyright (c) All rights reserved
 */


//在 Sylar 框架中，Servlet 是专门用于处理 HTTP 请求的逻辑模块，
// 它类似于 Web 编程中的“控制器”或“请求处理器”。每个 Servlet 对应
// 一个或一类 URI 路径，负责对请求进行解析、处理并生成响应结果。
// Sylar 提供了 Servlet 抽象类定义统一接口，所有具体的处理逻辑
// （如状态查看、配置查询等）都通过继承该类实现；而 ServletDispatch 
// 则是一个“路由器”，负责根据 URI 将请求分发到对应的 Servlet。
// 通过这种机制，Sylar 实现了模块化、可扩展的 HTTP 请求处理体系。
#ifndef __SYLAR_HTTP_SERVLET_H__
#define __SYLAR_HTTP_SERVLET_H__

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include "http.h"
#include "http_session.h"
#include "sylar/thread.h"
#include "sylar/util.h"

namespace sylar {
namespace http {

class Servlet {
public:
    typedef std::shared_ptr<Servlet> ptr;

    Servlet(const std::string& name) : m_name(name) {}
    virtual ~Servlet(){}

    //处理请求
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                            sylar::http::HttpResponse::ptr response,
                            sylar::http::HttpSession::ptr session) = 0;
    const std::string& getName() const { return m_name; }
protected:
    std::string m_name;
};


//FunctionServlet允许你不用写一个完整的类，只用一个函数(或lambda)就能处理一个HTTP请求，让开发变得更快速、更灵活
//函数式Servlet
//使用示例：将/hello的处理逻辑注册成一个lambda表达式
//m_dispatch->addServlet("/hello", FunctionServlet::ptr(new FunctionServlt([](){})));
class FunctionServlet : public Servlet {
public:
    std::shared_ptr<FunctionServlet> ptr;
    typedef std::function<int32_t(sylar::http::HttpRequest::ptr request, 
                                  sylar::http::HttpResponse::ptr response,
                                  sylar::http::HttpSession::ptr session)> callback;
    
    FunctionServlet(callback cb);
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                           sylar::http::HttpResponse::ptr response,
                           sylar::http::HttpSession::ptr session) override;
private:
    callback m_cb;
};


//IServletCreator：是一个接口类(抽象基类)，为了统一不同类型Servlet创建方式，
//为ServletDispatch提供统一的获取Servlet实例的机制
class IServletCreator {
public:
    typedef std::shared_ptr<IServletCreator> ptr;
    virtual ~IServletCreator(){}

    //get()的作用是获取一个Servlet实例
    //在不同的实现类中，该函数的具体行为不同
    virtual Servlet::ptr get() const = 0;
    virtual std::string getName() const = 0;
};


//HoldServletCreator类的作用是持有并返回一个已有的Servlet实例
//也就是说，每当ServletDispatch需要处理一个请求时，如果路径匹配到了HoldServletCreator
//他就会直接返回那个预先创建好的Servlet实例
class HoldServletCreator : public IServletCreator {
public:
    typedef std::shared_ptr<HoldServletCreator> ptr;
    //接受一个已创建的Servlet保存在m_servlet中，后续调用get时就返回这个对象
    HoldServletCreator(Servlet::ptr slt) : m_servlet(slt) {
    }
    Servlet::ptr get() const override {
        return m_servlet;
    }
    std::string getName() const override {
        return m_servlet->getName();
    }
private:
    Servlet::ptr m_servlet;
};

//每次调用都创建新实例
template<class T>
class ServletCreator : public IServletCreator {
public:
    typedef std::shared_ptr<ServletCreator> ptr;

    ServletCreator(){}
    Servlet::ptr get() const override {
        return Servlet::ptr(new T);
    }
    std::string getName() const override {
        return TypeToName<T>();
    }
};


//servlet分发器
class ServletDispatch : public Servlet {
public:
    typedef std::shared_ptr<ServletDispatch> ptr;
    typedef RWMutex RWMutexType;

    ServletDispatch();
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                           sylar::http::HttpResponse::ptr response,
                           sylar::http::HttpSession::ptr session) override;

    //添加servlet 精确
    void addServlet(const std::string& uri, Servlet::ptr slt);
    void addServlet(const std::string& uri, FunctionServlet::callback cb);
    //添加servlet 模糊
    void addGlobServlet(const std::string& uri, Servlet::ptr slt);
    void addGlobServlet(const std::string& uri, FunctionServlet::callback cb);

    //添加servlet servlet由传入的ServletCreator创建
    void addServletCreator(const std::string& uri, IServletCreator::ptr creator);
    void addGlobServletCreator(const std::string& uri, IServletCreator::ptr creator);

    //使用  servlet_dispatch->addServletCreator<MyServlet>("/usr/login")
    //他等价于 servlet_dispatch->addServletCreator("/usr/login", std::make_shared<MySrvlet>())
    template<class T>
    void addServletCreator(const std::string& uri) {
        addServletCreator(uri, std::make_shared<ServletCreator<T>>());
    }
    template<class T>
    void addGlobServletCreator(const std::string& uri) {
        addGlobServletCreator(uri, std::make_shared<ServletCreator<T>>())
    }

    void delServlet(const std::string& uri);
    void delGlobServlet(const std::string& uri);

    //返回、设置默认servlet
    Servlet::ptr getDefault() const { return m_default; }
    void setDefault(Servlet::ptr v) { m_default = v; }
    
    //通过uri获得与之匹配的servlet
    Servlet::ptr getServlet(const std::string& uri);
    Servlet::ptr getGlobServlet(const std::string& uri);
    //通过uri获得servlet，优先精确匹配，其次模糊匹配，最后返回默认
    Servlet::ptr getMatchedServlet(const std::string& uri);

    //将已经注册的servlet全部拷贝出来，放到一个外部传入的map中
    void listAllServletCreator(std::map<std::string, IServletCreator::ptr>& infos);
    void listAllGlobServletCreator(std::map<std::string, IServletCreator::ptr>& infos);



private:
    RWMutexType m_mutex;
    //一个精准的URI匹配器，将具体的UTI映射到对应的servlet对象
    //uri-servlet映射器
    std::unordered_map<std::string, IServletCreator::ptr> m_datas;
    //一个模糊URI匹配器，用来支持路径前缀匹配，比如我们注册了/sylar/*，就会匹配/sylar/xx,/sylar/abc/123等路径
    std::vector<std::pair<std::string, IServletCreator::ptr>> m_globs;
    //默认的servlet，当所有精确匹配和模糊匹配都失败时，用他来处理请求
    Servlet::ptr m_default;
};


class NotFoundServlet : public Servlet {
public:
    typedef std::shared_ptr<NotFoundServlet> ptr;
    NotFoundServlet(const std::string& name);
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                           sylar::http::HttpResponse::ptr response,
                           sylar::http::HttpSession::ptr session) override;
private:
    std::string m_name;
    std::string m_content;
};

}
}


#endif



