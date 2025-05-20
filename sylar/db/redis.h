/**
* @file reids.h
* @brief Redis 客户端封装接口，支持同步与异步、单节点与集群模式
* @date 2025-05-15
*/

// IRedis(接口，定义 cmd)
//          |
// --------------------
// |                   |
// ISyncRedis          FoxRedis / FoxRedisCluster
// |
// -------------------- -
// |                   |
// Redis          RedisCluster

//协程线程混合模型
//            +---------------------------+
//            |      任务提交入口         |
//            +------------+--------------+
//                         |
//                         v
//             +----------------------+
//             |     线程池（N个线程） |
//             +----------------------+
//                   /       |       \
//                  /        |        \
//                 v         v         v
//         +----------+ +----------+ +----------+
//         | Thread 1 | | Thread 2 | | Thread N |
//         +----------+ +----------+ +----------+
//              |            |            |
//   ...........|............|............|...........
//   :          v            v            v         :
//   :   +------------+ +------------+ +------------+ :
//   :   | 协程调度器 | | 协程调度器 | | 协程调度器 | :
//   :   +------------+ +------------+ +------------+ :
//   :        |               |               |       :
//   :   ~ ~ ~ ~ ~ ~     ~ ~ ~ ~ ~ ~     ~ ~ ~ ~ ~ ~  :
//   :  | 协程1     |   | 协程1     |   | 协程1     | :
//   :  | 协程2     |   | 协程2     |   | 协程2     | :
//   :  | ...       |   | ...       |   | ...       | :
//   :   ~ ~ ~ ~ ~ ~     ~ ~ ~ ~ ~ ~     ~ ~ ~ ~ ~ ~  :
//   ..................................................
// 协程线程混合模型将多线程和协程紧密结合，在多个线程（如I/O线程、业务线程）
// 中并行执行，而每个线程内部通过协程实现轻量级的伪并发。线程由操作系统调度，
// 负责分配计算资源和并行处理任务；协程则在单个线程内通过主动挂起（Yield）和
// 恢复（Resume）高效切换执行上下文，避免线程阻塞。当协程发起异步操作时，会
// 主动挂起自身，线程则继续调度同线程中其他协程执行，极大提升资源利用率；异步
// 事件完成后，调度器唤醒对应协程恢复执行。这样，多线程提供了并发能力，协程保
// 证了灵活高效的任务切换，两者协同配合，实现了高并发网络编程的最佳性能。可以
// 形象地理解为：多个线程就像多个舞台同时运作，每个舞台上演员（协程）轮流登场
// 表演，既保证了整体并行，也兼顾了单舞台内的流畅切换。


#ifndef __SYLAR_DB_REDIS_H__
#define __SYLAR_DB_REDIS_H__

#include <stdlib.h>
#include <hiredis-vip/hiredis.h>
#include <hiredis-vip/hircluster.h>
#include <hiredis-vip/adapters/libevent.h>
#include <sys/time.h>
#include <string>
#include <memory>
#include "sylar/mutex.h"
#include "sylar/db/fox_thread.h"
#include "sylar/singleton.h"

namespace sylar {

typedef std::shared_ptr<redisReply> ReplyPtr;

/**
 * @brief IRedis 是 Redis 客户端接口的基类，定义了所有 Redis 客户端实现应支持的通用命令接口。
 *
 * 该类提供了三种形式的 cmd 命令发送方式（格式化字符串、va_list 参数列表、字符串数组），
 * 并提供了客户端名称、密码和类型的设置与获取接口。
 *
 * 所有继承该类的 Redis 实现（包括同步、异步、单节点、集群等）都应实现这些接口。
 */
class IRedis {
public:
    /**
     * @brief Redis 客户端类型枚举
     */
    enum Type {
        REDIS = 1,              ///< 单机 Redis，同步模式
        REDIS_CLUSTER = 2,      ///< Redis Cluster，同步模式
        FOX_REDIS = 3,          ///< 单机 Redis，异步 FoxThread 模式
        FOX_REDIS_CLUSTER = 4   ///< Redis Cluster，异步 FoxThread 模式
    };

    typedef std::shared_ptr<IRedis> ptr;

    /**
     * @brief 默认构造函数，日志开关默认启用
     */
    IRedis() : m_logEnable(true) { }

    /**
     * @brief 虚析构函数
     */
    virtual ~IRedis() {}

    /**
     * @brief 执行 Redis 命令（格式化字符串方式）
     * @param fmt 格式化字符串
     * @return 命令返回结果指针
     */
    virtual ReplyPtr cmd(const char* fmt, ...) = 0;

    /**
     * @brief 执行 Redis 命令（va_list 方式）
     * @param fmt 格式化字符串
     * @param ap 可变参数列表
     * @return 命令返回结果指针
     */
    virtual ReplyPtr cmd(const char* fmt, va_list ap) = 0;

    /**
     * @brief 执行 Redis 命令（参数向量方式）
     * @param argv 字符串参数数组
     * @return 命令返回结果指针
     */
    virtual ReplyPtr cmd(const std::vector<std::string>& argv) = 0;

    /**
     * @brief 获取客户端名称
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 设置客户端名称
     */
    void setName(const std::string& v) { m_name = v; }

    /**
     * @brief 获取 Redis 认证密码
     */
    const std::string& getPasswd() const { return m_passwd; }

    /**
     * @brief 设置 Redis 认证密码
     */
    void setPasswd(const std::string& v) { m_passwd = v; }

    /**
     * @brief 获取客户端类型
     */
    Type getType() const { return m_type; }

protected:
    std::string m_name;       ///< 客户端名称标识
    std::string m_passwd;     ///< Redis 认证密码
    Type m_type;              ///< 客户端类型
    bool m_logEnable;         ///< 是否启用日志记录
};


/**
 * @brief ISyncRedis 是 IRedis 的同步客户端接口扩展，提供连接管理与流水线支持的抽象接口。
 *
 * 该类定义了适用于同步模式下的 Redis 客户端操作接口，包括连接建立、断线重连、
 * 命令追加（pipeline）、响应提取、超时设置等功能。
 *
 * 所有基于同步调用的 Redis 客户端实现应继承此类，并提供对应功能的实现。
 */
class ISyncRedis : public IRedis {
public:
    typedef std::shared_ptr<ISyncRedis> ptr;

    /**
     * @brief 虚析构函数
     */
    virtual ~ISyncRedis() {}

    /**
     * @brief 重新连接 Redis 服务器（使用已有连接参数）
     * @return 是否连接成功
     */
    virtual bool reconnect() = 0;

    /**
     * @brief 使用指定 IP 和端口建立连接
     * @param ip Redis 服务地址
     * @param port Redis 服务端口
     * @param ms 连接超时时间（毫秒），默认值为 0 表示不设置
     * @return 是否连接成功
     */
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0) = 0;

    /**
     * @brief 使用内部存储的连接参数建立连接
     * @return 是否连接成功
     */
    virtual bool connect() = 0;

    /**
     * @brief 设置命令或连接的超时时间
     * @param ms 超时时间（毫秒）
     * @return 是否设置成功
     */
    virtual bool setTimeout(uint64_t ms) = 0;

    /**
     * @brief 将命令追加到发送缓冲区（格式化字符串方式）
     * @param fmt 格式化字符串
     * @return 追加成功返回 0，否则返回非 0
     */
    virtual int appendCmd(const char* fmt, ...) = 0;

    /**
     * @brief 将命令追加到发送缓冲区（va_list 方式）
     * @param fmt 格式化字符串
     * @param ap 可变参数列表
     * @return 追加成功返回 0，否则返回非 0
     */
    virtual int appendCmd(const char* fmt, va_list ap) = 0;

    /**
     * @brief 将命令追加到发送缓冲区（参数向量方式）
     * @param argv 字符串参数数组
     * @return 追加成功返回 0，否则返回非 0
     */
    virtual int appendCmd(const std::vector<std::string>& argv) = 0;

    /**
     * @brief 获取上一个追加命令的执行结果（阻塞直到获取到）
     * @return Redis 回复对象的智能指针
     */
    virtual ReplyPtr getReply() = 0;

    /**
     * @brief 获取上次活跃时间（如最后一次发送/接收命令的时间戳）
     * @return 时间戳（毫秒）
     */
    uint64_t getLastActiveTime() const { return m_lastActiveTime; }

    /**
     * @brief 设置上次活跃时间
     * @param v 时间戳（毫秒）
     */
    void setLastActiveTime(uint64_t v) { m_lastActiveTime = v; }

protected:
    uint64_t m_lastActiveTime; ///< 最近一次活跃时间（毫秒时间戳）
};


// Redis 同步客户端封装类，实现了 ISyncRedis 接口
class Redis : public ISyncRedis {
public:
    // 智能指针别名，方便管理 Redis 对象生命周期
    typedef std::shared_ptr<Redis> ptr;

    // 默认构造函数
    Redis();

    // 使用配置构造 Redis 对象，配置项一般包括 IP、端口、超时时间等
    Redis(const std::map<std::string, std::string>& conf);

    // 重新连接 Redis 服务器
    virtual bool reconnect();

    // 使用指定 IP 和端口连接 Redis，可选超时时间（毫秒）
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);

    // 使用类内部保存的 host 和 port 连接 Redis
    virtual bool connect();

    // 设置命令的超时时间（毫秒）
    virtual bool setTimeout(uint64_t ms);

    // 发送格式化命令并获取结果（变参版本）
    virtual ReplyPtr cmd(const char* fmt, ...);

    // 发送格式化命令并获取结果（va_list 版本）
    virtual ReplyPtr cmd(const char* fmt, va_list ap);

    // 通过字符串数组形式发送命令并获取结果（避免格式化，推荐使用）
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    // 追加格式化命令（变参版本），适用于流水线操作
    virtual int appendCmd(const char* fmt, ...);

    // 追加格式化命令（va_list 版本），适用于流水线操作
    virtual int appendCmd(const char* fmt, va_list ap);

    // 追加字符串数组命令，适用于流水线操作
    virtual int appendCmd(const std::vector<std::string>& argv);

    // 获取之前 append 的命令的返回值（用于流水线）
    virtual ReplyPtr getReply();

private:
    std::string m_host;                    // Redis 服务器地址
    uint32_t m_port;                       // Redis 服务器端口
    uint32_t m_connectMs;                  // 连接超时时间（毫秒）
    struct timeval m_cmdTimeout;           // 命令超时时间（timeval 结构体）
    std::shared_ptr<redisContext> m_context; // hiredis 提供的 Redis 连接上下文
};

/**
 * @brief Redis Cluster 同步客户端封装类，实现了 ISyncRedis 接口
 *        基于 hiredis-vip 的 redisClusterContext 封装，支持 Redis Cluster 多节点访问。
 */
class RedisCluster : public ISyncRedis {
public:
    typedef std::shared_ptr<RedisCluster> ptr;

    /**
     * @brief 默认构造函数，需手动调用 connect() 建立连接
     */
    RedisCluster();

    /**
     * @brief 通过配置参数初始化 Redis Cluster 客户端
     * @param conf 配置字典，支持 "host"、"port"、"timeout" 等字段
     */
    RedisCluster(const std::map<std::string, std::string>& conf);

    /**
     * @brief 重新连接 Redis Cluster，使用已有配置参数
     * @return 是否连接成功
     */
    virtual bool reconnect();

    /**
     * @brief 连接指定地址的 Redis Cluster 节点（仅用于初始握手）
     * @param ip Redis 节点 IP 地址
     * @param port Redis 节点端口
     * @param ms 连接超时时间（毫秒），默认为 0 表示不设置
     * @return 是否连接成功
     */
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);

    /**
     * @brief 使用内部配置参数连接 Redis Cluster
     * @return 是否连接成功
     */
    virtual bool connect();

    /**
     * @brief 设置 Redis 命令调用的超时时间
     * @param ms 超时时间（毫秒）
     * @return 是否设置成功
     */
    virtual bool setTimeout(uint64_t ms);

    /**
     * @brief 发送格式化命令并等待响应（变参方式）
     * @param fmt 格式化字符串
     * @return Redis 回复对象的智能指针
     */
    virtual ReplyPtr cmd(const char* fmt, ...);

    /**
     * @brief 发送格式化命令并等待响应（va_list 参数方式）
     * @param fmt 格式化字符串
     * @param ap 参数列表
     * @return Redis 回复对象的智能指针
     */
    virtual ReplyPtr cmd(const char* fmt, va_list ap);

    /**
     * @brief 使用字符串数组发送命令（推荐使用）
     * @param argv 命令及参数的字符串数组
     * @return Redis 回复对象的智能指针
     */
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    /**
     * @brief 将命令追加至发送队列（格式化字符串方式，适用于 pipeline）
     * @param fmt 格式化字符串
     * @return 成功返回 0，失败返回非 0
     */
    virtual int appendCmd(const char* fmt, ...);

    /**
     * @brief 将命令追加至发送队列（va_list 参数方式）
     * @param fmt 格式化字符串
     * @param ap 参数列表
     * @return 成功返回 0，失败返回非 0
     */
    virtual int appendCmd(const char* fmt, va_list ap);

    /**
     * @brief 将命令追加至发送队列（字符串数组方式）
     * @param argv 命令及参数的字符串数组
     * @return 成功返回 0，失败返回非 0
     */
    virtual int appendCmd(const std::vector<std::string>& argv);

    /**
     * @brief 获取上一个追加命令的执行结果（阻塞等待）
     * @return Redis 回复对象的智能指针
     */
    virtual ReplyPtr getReply();

private:
    std::string m_host; ///< Redis Cluster 初始连接的主机地址
    uint32_t m_port; ///< Redis Cluster 初始连接端口
    uint32_t m_connectMs; ///< 连接超时时间（毫秒）
    struct timeval m_cmdTimeout; ///< 命令超时时间（timeval 格式）
    std::shared_ptr<redisClusterContext> m_context; ///< hiredis-vip 提供的 Cluster 连接上下文
};


/**
 * @brief 异步 Redis 客户端封装类，基于 hiredis 异步接口 + libevent 实现
 *        与 Sylar 框架中的协程、调度器和线程模型配合工作，实现协程非阻塞 Redis 调用。
 */
class FoxRedis : public IRedis {
public:
    typedef std::shared_ptr<FoxRedis> ptr;

    /**
     * @brief 连接状态枚举
     */
    enum STATUS {
        UNCONNECTED = 0, ///< 未连接
        CONNECTING = 1,  ///< 连接中
        CONNECTED = 2    ///< 已连接
    };

    /**
     * @brief 命令执行结果枚举
     */
    enum RESULT {
        OK = 0,           ///< 成功
        TIME_OUT = 1,     ///< 超时
        CONNECT_ERR = 2,  ///< 连接失败
        CMD_ERR = 3,      ///< 命令错误
        REPLY_NULL = 4,   ///< 响应为空
        REPLY_ERR = 5,    ///< 响应格式错误
        INIT_ERR = 6      ///< 初始化失败
    };

    /**
     * @brief 构造函数
     * @param thr 所属 FoxThread（线程封装）
     * @param conf Redis 配置项，如 host、port、timeout 等
     */
    FoxRedis(sylar::FoxThread* thr, const std::map<std::string, std::string>& conf);

    /**
     * @brief 析构函数
     */
    ~FoxRedis();

    /**
     * @brief 发送格式化命令（变参方式）
     * @param fmt 格式化字符串
     * @return Redis 回复对象
     */
    virtual ReplyPtr cmd(const char* fmt, ...);

    /**
     * @brief 发送格式化命令（va_list 参数方式）
     * @param fmt 格式化字符串
     * @param ap 参数列表
     * @return Redis 回复对象
     */
    virtual ReplyPtr cmd(const char* fmt, va_list ap);

    /**
     * @brief 发送命令（参数数组方式）
     * @param argv 命令参数数组
     * @return Redis 回复对象
     */
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    /**
     * @brief 初始化连接，内部调用 pinit()
     * @return 是否初始化成功
     */
    bool init();

    /**
     * @brief 获取当前上下文数量（可能用于统计活跃命令数）
     * @return 当前上下文数量
     */
    int getCtxCount() const { return m_ctxCount; }

private:
    /**
     * @brief 认证回调函数，内部用于连接后发送 AUTH 命令（如配置中有密码）
     */
    // 在使用 hiredis-vip 异步客户端时，通常需要通过如下代码发起认证命令：
    // redisAsyncCommand(c, &FoxRedis::OnAuthCb, this, "AUTH %s", password.c_str());
    // 其中 &FoxRedis::OnAuthCb 是回调函数，this 会作为 priv 传入。
    // 所以，这个回调函数的作用是：在异步连接 Redis 成功后，处理 AUTH 命令的认证结果，并记录是否认证成功。
    static void OnAuthCb(redisAsyncContext* c, void* rp, void* priv);

    /**
     * @brief 表示一次协程发起的 Redis 调用上下文
     */
    struct FCtx {
        std::string cmd;             ///< 发送的命令文本（调试用）
        sylar::Scheduler* scheduler;///< 所属调度器
        sylar::Fiber::ptr fiber;    ///< 当前协程（挂起后 resume 用）
        ReplyPtr rpy;               ///< 命令结果（Reply 对象）
    };

    /**
     * @brief 封装一条命令的 libevent 超时处理和回调绑定
     */
    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;              ///< libevent 的事件指针
        bool timeout;           ///< 是否已超时
        FoxRedis* rds;          ///< 所属的 Redis 客户端
        std::string cmd;        ///< 命令内容（日志或调试用）
        FCtx* fctx;             ///< 协程上下文指针（真正执行命令的 fiber）
        FoxThread* thread;      ///< 所在线程指针

        Ctx(FoxRedis* rds);     ///< 构造函数
        ~Ctx();                 ///< 析构函数

        /**
         * @brief 初始化超时事件
         * @return 是否初始化成功
         */
        bool init();

        /**
         * @brief 取消注册事件（如提前完成命令）
         */
        void cancelEvent();

        /**
         * @brief 超时事件触发回调
         * @param fd 文件描述符
         * @param event 事件类型
         * @param d Ctx 对象指针
         */
        static void EventCb(int fd, short event, void* d);
    };

    /**
     * @brief 实际执行命令的接口，会将当前协程挂起并注册超时事件
     * @param ctx 协程上下文
     */
    virtual void pcmd(FCtx* ctx);

    /**
     * @brief 实际的连接初始化逻辑，初始化 hiredis 异步连接与事件绑定
     * @return 是否成功
     */
    bool pinit();

    /**
     * @brief 延迟销毁 redisAsyncContext，避免在回调中直接 free
     * @param c Redis 异步上下文
     */
    void delayDelete(redisAsyncContext* c);

    /**
     * @brief Redis 连接建立回调
     */
    //redisAsyncSetConnectCallback
    static void ConnectCb(const redisAsyncContext* c, int status);

    /**
     * @brief Redis 连接断开回调
     */
    //redisAsyncSetDisconnectCallback
    static void DisconnectCb(const redisAsyncContext* c, int status);

    /**
     * @brief Redis 命令执行完成后的回调
     * @param c hiredis 异步上下文
     * @param r 回复指针
     * @param privdata 私有数据（FCtx 指针）
     */
    //它是在你调用 redisAsyncCommand() 发送命令时传入的回调
    static void CmdCb(redisAsyncContext* c, void* r, void* privdata);

    /**
     * @brief 超时定时器回调
     */
    //它是一个被定时触发的超时回调，但作用是周期性向 Redis 发送 ping 命令，起到“连接保活”的作用。
    static void TimeCb(int fd, short event, void* d);

private:
    sylar::FoxThread* m_thread;                      ///< 所属线程
    std::shared_ptr<redisAsyncContext> m_context;    ///< hiredis 的异步连接对象
    std::string m_host;                              ///< Redis 服务地址
    uint16_t m_port;                                 ///< Redis 服务端口
    STATUS m_status;                                 ///< 当前连接状态
    int m_ctxCount;                                  ///< 当前活跃上下文数量（用于统计）

    struct timeval m_cmdTimeout;                     ///< 命令执行超时时间
    std::string m_err;                               ///< 错误信息缓存
    struct event* m_event;                           ///< 全局事件对象（可能用于全局超时控制）
};

/**
 * @brief Redis 集群客户端封装类，基于 hiredis-vip 异步集群接口 + libevent，
 *        支持自动 slot 路由，适用于 Redis Cluster 场景，协程非阻塞式访问。
 */
class FoxRedisCluster : public IRedis {
public:
    typedef std::shared_ptr<FoxRedisCluster> ptr;

    /**
     * @brief 连接状态
     */
    enum STATUS {
        UNCONNECTED = 0, ///< 未连接
        CONNECTING = 1,  ///< 正在连接
        CONNECTED = 2    ///< 已连接
    };

    /**
     * @brief 命令执行结果
     */
    enum RESULT {
        OK = 0,           ///< 成功
        TIME_OUT = 1,     ///< 超时
        CONNECT_ERR = 2,  ///< 连接错误
        CMD_ERR = 3,      ///< 命令错误
        REPLY_NULL = 4,   ///< 响应为空
        REPLY_ERR = 5,    ///< 响应格式错误
        INIT_ERR = 6      ///< 初始化失败
    };

    /**
     * @brief 构造函数
     * @param thr 所属线程对象
     * @param conf 配置信息，如 host、timeout 等
     */
    FoxRedisCluster(sylar::FoxThread* thr, const std::map<std::string, std::string>& conf);

    /**
     * @brief 析构函数
     */
    ~FoxRedisCluster();

    /**
     * @brief 发送格式化命令
     */
    virtual ReplyPtr cmd(const char* fmt, ...);

    /**
     * @brief 使用 va_list 参数发送格式化命令
     */
    virtual ReplyPtr cmd(const char* fmt, va_list ap);

    /**
     * @brief 使用参数数组发送命令
     */
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    /**
     * @brief 获取当前活跃命令上下文数量
     */
    int getCtxCount() const { return m_ctxCount; }

    /**
     * @brief 初始化连接，建立与 Redis Cluster 的异步连接
     */
    bool init();

private:
    /**
     * @brief 协程上下文，封装一次命令请求
     */
    struct FCtx {
        std::string cmd;              ///< 命令内容
        sylar::Scheduler* scheduler; ///< 所属调度器
        sylar::Fiber::ptr fiber;     ///< 当前协程
        ReplyPtr rpy;                ///< 命令响应结果
    };

    /**
     * @brief 每条命令的超时管理、事件绑定
     */
    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;               ///< 超时事件
        bool timeout;            ///< 是否已超时
        FoxRedisCluster* rds;   ///< 所属客户端实例
        FCtx* fctx;              ///< 协程上下文
        std::string cmd;         ///< 命令内容
        FoxThread* thread;       ///< 所在线程

        void cancelEvent();      ///< 取消事件（提前完成）

        Ctx(FoxRedisCluster* rds);  ///< 构造函数
        ~Ctx();                      ///< 析构函数
        bool init();                ///< 初始化超时事件
        static void EventCb(int fd, short event, void* d); ///< 超时事件回调
    };

private:
    virtual void pcmd(FCtx* ctx); ///< 实际发起命令
    bool pinit();                 ///< 初始化连接
    void delayDelete(redisAsyncContext* c); ///< 延迟释放连接

    static void OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv); ///< 认证回调
    static void ConnectCb(const redisAsyncContext* c, int status);           ///< 连接成功回调
    static void DisconnectCb(const redisAsyncContext* c, int status);        ///< 断开连接回调
    static void CmdCb(redisClusterAsyncContext* c, void* r, void* privdata); ///< 命令回调
    static void TimeCb(int fd, short event, void* d);                        ///< 超时回调

private:
    sylar::FoxThread* m_thread;                      ///< 所在线程
    std::shared_ptr<redisClusterAsyncContext> m_context; ///< hiredis-vip 异步连接上下文
    std::string m_host;                              ///< 主机地址（可包含多个）
    STATUS m_status;                                 ///< 当前状态
    int m_ctxCount;                                  ///< 活跃上下文计数

    struct timeval m_cmdTimeout;                     ///< 命令超时时间
    std::string m_err;                               ///< 错误信息
    struct event* m_event;                           ///< libevent 事件（全局定时器等）
};

/**
 * @brief Redis 客户端管理器，支持多命名客户端池化复用，线程安全。
 */
class RedisManager {
public:
    /**
     * @brief 构造函数
     */
    RedisManager();

    /**
     * @brief 获取一个指定名称的 Redis 客户端
     * @param name 客户端名称（与配置项匹配）
     * @return Redis 客户端接口指针
     */
    IRedis::ptr get(const std::string& name);

    /**
     * @brief 打印 RedisManager 的内部状态
     */
    std::ostream& dump(std::ostream& os);

private:
    /**
     * @brief 对象复用（资源池回收）。
     */
    void freeRedis(IRedis* r);

    /**
     * @brief 初始化 Redis 客户端池（从配置中加载）
     */
    void init();

private:
    sylar::RWMutex m_mutex; ///< 读写锁，保护资源访问

    /**
     * @brief 客户端池（name -> list of clients）
     */
    std::map<std::string, std::list<IRedis*> > m_datas;

    /**
     * @brief 配置项（name -> conf map）
     */
    std::map<std::string, std::map<std::string, std::string> > m_config;
};

/**
 * @brief RedisManager 的单例对象，使用 sylar::Singleton 模板封装
 */
typedef sylar::Singleton<RedisManager> RedisMgr;

/**
 * @brief Redis 命令工具类，简化调用，支持重试逻辑
 */
class RedisUtil {
public:
    /**
     * @brief 发送格式化命令
     * @param name Redis 实例名称（对应 RedisManager 中的 key）
     */
    static ReplyPtr Cmd(const std::string& name, const char* fmt, ...);

    /**
     * @brief 使用 va_list 参数发送命令
     */
    static ReplyPtr Cmd(const std::string& name, const char* fmt, va_list ap);

    /**
     * @brief 使用参数数组方式发送命令
     */
    static ReplyPtr Cmd(const std::string& name, const std::vector<std::string>& args);

    /**
     * @brief 多次尝试发送命令，直到成功或超过次数
     * @param name Redis 实例名称
     * @param count 最大重试次数
     */
    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const char* fmt, ...);

    /**
     * @brief 参数数组方式的 TryCmd
     */
    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args);
};


}

#endif
