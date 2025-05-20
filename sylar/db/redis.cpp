#include "redis.h"
#include "sylar.h"
#include "sylar/log.h"


namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static sylar::ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_redis =
       sylar::Config::Lookup("redis.config", std::map<std::string, std::map<std::string, std::string> >(), "redis config");

static std::string get_value(const std::map<std::string, std::string>& m
                             ,const std::string& key
                             ,const std::string& def = "") {
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

// 创建一个与输入 redisReply 对象 r 完全独立但内容相同的新对象 c
redisReplay* RedisReplayClone(redisReplay* r) {
    redisReplay* c = (redisReplay*)calloc(1, sizeof(*c));
    c->type = r->type;
    switch(r->type) {
        case REDIS_REPLY_INTEGER:
            c->integer = r->integer;
            break;
        case REDIS_REPLY_ARRAY:
            if(r->element != NULL && r->elements > 0) {
                c->element = (redisReply**)calloc(r->elements, sizeof(r));
                c->elements = r->elements;
                for(size_t i = 0; i < r->elements; ++i) {
                    c->element[i] = RedisReplyClone(r->element[i]);
                }
            }
            break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
            if(r->str == NULL) {
                c->str = NULL;
            } else {
                c->str = (char*)malloc(r->len + 1);
                memcpy(c->str, r->str, r->len);
                c->str[r->len] = '\0';
            }
            c->len = r->len;
            break;        
    }
    return c;
}

Redis::Redis() {
    m_type = IRedis::REDIS;
}

Redis::Redis(const std::map<std::string, std::string>& conf) {
    m_type = IRedis::REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    m_host = tmp.substr(0, pos);
    m_port = sylar::TypeUtil::Atoi(tmp.substr(pos + 1));
    m_passwd = get_value(conf, "passwd");
    m_logEnable = sylar::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = sylar::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}


bool Redis::reconnect() {
    return redisReconnect(m_context.get());
}

bool Redis::connect() {
    return connect(m_host, m_port, 50);
}

bool Redis::connect(const std::string& ip, int port, uint64_t ms) {
    m_host = ip;
    m_port = port;
    m_connectMs = ms;
    if(m_context) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisConnectWithTimeout(ip.c_str(), port, tv);
    if(c) {
        if(m_cmdTimeout.tv_sec || m_cmdTimeout.tv_usec) {
            setTimeout(m_cmdTimeout.tv_sec * 1000 + m_cmdTimeout.tv_usec / 1000);
        }
        m_context.reset(c, redisFree);

        if(!m_passwd.empty()) {
            auto r = (redisReply*)redisCommand(c, "auth %s", m_passwd.c_str());
            if(!r) {
                SYLAR_LOG_ERROR(g_logger) << "auth error:("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(r->type != REDIS_REPLY_STATUS) {
                SYLAR_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(!r->str) {
                SYLAR_LOG_ERROR(g_logger) << "auth reply str error: NULL("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}

ReplyPtr Redis::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr Redis::cmd(const char* fmt, va_list ap) {
    auto r = (redisReply*)redisvCommand(m_context.get(), fmt, ap);
    if(!r) {
        if(m_logEnable) {
            SYLAR_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << ": " << r->str;
    }
    return nullptr;
}


ReplyPtr Redis::cmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
    if(!r) {
        if(m_logEnable) {
            SYLAR_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << r->str;
    }
    return nullptr;
}

ReplyPtr Redis::getReply() {
    redisReply* r = nullptr;
    if(redisGetReply(m_context.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisGetReply error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
    }
    return nullptr;
}


int Redis::appendCmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;
}

int Redis::appendCmd(const char* fmt, va_list ap) {
    return redisvAppendCommand(m_context.get(), fmt, ap);
}

RedisCluster::RedisCluster() {
    m_type = IRedis::REDIS_CLUSTER;
}

RedisCluster::RedisCluster(const std::map<std::string, std::string>& conf) {
    m_type = IRedis::REDIS_CLUSTER;
    m_host = get_value(conf, "host");
    m_passwd = get_value(conf, "passwd");
    m_logEnable = sylar::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = sylar::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

bool RedisCluster::reconnect() {
    return true;
}

bool RedisCluster::connect() {
    return connect(m_host, m_port, 50);
}

bool RedisCluster::connect(const std::string& ip, int port, uint64_t ms) {
    m_host = ip;
    m_port = port;
    m_connectMs = ms;
    if(m_context) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisClusterConnectWithTimeout(ip.c_str(), tv, 0);
    if(c) {
        m_context.reset(c, redisClusterFree);
        if(!m_passwd.empty()) {
            auto r = (redisReply*)redisClusterCommand(c, "auth %s", m_passwd.c_str());
            if(!r) {
                SYLAR_LOG_ERROR(g_logger) << "auth error:("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(r->type != REDIS_REPLY_STATUS) {
                SYLAR_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(!r->str) {
                SYLAR_LOG_ERROR(g_logger) << "auth reply str error: NULL("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}


bool RedisCluster::setTimeout(uint64_t ms) {
    return true;
}

ReplyPtr RedisCluster::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}


ReplyPtr RedisCluster::cmd(const char* fmt, va_list ap) {
    auto r = (redisReply*)redisClustervCommand(m_context.get(), fmt, ap);
    if(!r) {
        if(m_logEnable) {
            SYLAR_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << ": " << r->str;
    }
    return nullptr;
}

ReplyPtr RedisCluster::cmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisClusterCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
    if(!r) {
        if(m_logEnable) {
            SYLAR_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << r->str;
    }
    return nullptr;
}

ReplyPtr RedisCluster::getReply() {
    redisReply* r = nullptr;
    if(redisClusterGetReply(m_context.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if(m_logEnable) {
        SYLAR_LOG_ERROR(g_logger) << "redisGetReply error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
    }
    return nullptr;
}

int RedisCluster::appendCmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;

}

int RedisCluster::appendCmd(const char* fmt, va_list ap) {
    return redisClustervAppendCommand(m_context.get(), fmt, ap);
}

int RedisCluster::appendCmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }
    return redisClusterAppendCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
}

FoxRedis::FoxRedis(sylar::FoxThread* thr, const std::map<std::string, std::string>& conf)
    :m_thread(thr)
    ,m_status(UNCONNECTED)
    ,m_event(nullptr) {
    m_type = IRedis::FOX_REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    m_host = tmp.substr(0, pos);
    m_port = sylar::TypeUtil::Atoi(tmp.substr(pos + 1));
    m_passwd = get_value(conf, "passwd");
    m_ctxCount = 0;
    m_logEnable = sylar::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = sylar::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

/**
 * @brief Redis 认证回调函数
 * 
 * 当连接成功后调用 redisAsyncCommand 执行 AUTH 命令，
 * 该回调用于处理 AUTH 命令的返回结果。
 * - 如果认证成功（返回 OK），记录日志；
 * - 否则输出认证失败的错误信息。
 */
void FoxRedis::OnAuthCb(redisAsyncContext* c, void* rp, void* priv) {
    FoxRedis* fr = (FoxRedis*)priv;
    redisReply* r = (redisReply*)rp;
    if(!r) {
        SYLAR_LOG_ERROR(g_logger) << "auth error:("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(r->type != REDIS_REPLY_STATUS) {
        SYLAR_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(!r->str) {
        SYLAR_LOG_ERROR(g_logger) << "auth reply str error: NULL("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(strcmp(r->str, "OK") == 0) {
        SYLAR_LOG_INFO(g_logger) << "auth ok: " << r->str << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
    } else {
        SYLAR_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
    }    
}

/**
 * @brief Redis 异步连接建立回调函数
 * 
 * 当调用 redisAsyncConnect 发起连接后触发。
 * - 若连接成功，更新状态为 CONNECTED；
 * - 如果设置了密码，继续发送 AUTH 命令，并设置 OnAuthCb 回调；
 * - 否则记录连接失败信息，状态设为 UNCONNECTED。
 */
void FoxRedis::ConnectCb(const redisAsyncContext* c, int status) {
    FoxRedis* ar = static_cast<FoxRedis*>(c->data);
    if(!status) {
        SYLAR_LOG_INFO(g_logger) << "FoxRedis::ConnectCb "
                   << c->c.tcp.host << ":" << c->c.tcp.port
                   << " success";
        ar->m_status = CONNECTED;
        if(!ar->m_passwd.empty()) {
            int rt = redisAsyncCommand(ar->m_context.get(), FoxRedis::OnAuthCb, ar, "auth %s", ar->m_passwd.c_str());
            if(rt) {
                SYLAR_LOG_ERROR(g_logger) << "FoxRedis Auth fail: " << rt;
            }
        }

    } else {
        SYLAR_LOG_ERROR(g_logger) << "FoxRedis::ConnectCb "
                    << c->c.tcp.host << ":" << c->c.tcp.port
                    << " fail, error:" << c->errstr;
        ar->m_status = UNCONNECTED;
    }
}

/**
 * @brief Redis 异步连接断开回调函数
 * 
 * 当连接被关闭（断开、超时、出错）时触发。
 * - 简单记录日志，并将连接状态更新为 UNCONNECTED。
 */
void FoxRedis::DisconnectCb(const redisAsyncContext* c, int status) {
    SYLAR_LOG_INFO(g_logger) << "FoxRedis::DisconnectCb "
               << c->c.tcp.host << ":" << c->c.tcp.port
               << " status:" << status;
    FoxRedis* ar = static_cast<FoxRedis*>(c->data);
    ar->m_status = UNCONNECTED;
}

/**
 * @brief Redis 命令异步执行回调函数
 * 
 * 每次通过 redisAsyncCommand 发送命令后，Redis 返回结果时触发。
 * - 若发生错误（连接异常、无 reply、reply 是 error），记录日志并恢复业务协程；
 * - 若命令成功，克隆 reply 结果保存，并恢复协程继续执行；
 * - 最后清理命令上下文 ctx 并取消事件（如超时定时器）。
 */
// Redis 异步命令执行 + 协程挂起模型
// [业务线程]
//    |
//    |---[发起 redisAsyncCommand] --> 返回
//    |---[Fiber::Yield()] --> 当前协程让出执行
//    |
// [事件循环线程]
//    |
//    |---[收到 Redis 响应]
//         |
//         |---[调用 CmdCb]
//                 |
//                 |---[唤醒原协程 Fiber]（将其重新调度）
//举例
// 一、异步调用阶段（立即返回，不阻塞线程）
// 我们调用 redisAsyncCommand(...) 向 Redis 发出一个异步命令。
// 这不会阻塞线程，仅是将命令写入 socket 并返回。
// Redis 服务器执行该命令可能是“立即”也可能“稍后”，但我们此时不关心结果，继续执行其他任务。
// 二、结果需要用时 —— 就得“等结果”
// 有些场景，比如：
// std::string val = redis->get("key");
// doSomething(val);
// 你就必须等到命令执行完才继续往下跑。
// 那就出现了两个选择
// 方案 1：使用条件变量（传统线程阻塞）
//  std::mutex m;
//  std::condition_variable cv;
//  bool done = false;
//  redisAsyncCommand(..., [](reply){
//      // 回调
//      result = reply;
//      done = true;
//      cv.notify_one();
//  });
//  std::unique_lock<std::mutex> lock(m);
//  cv.wait(lock, []{ return done; });
//缺点：阻塞一个系统线程，在高并发下代价大，资源浪费严重。
// 方案 2：使用协程挂起等待（线程继续做别的事）？
//  ctx->fctx->fiber = Fiber::GetThis();
//  ctx->fctx->scheduler = scheduler;
//  redisAsyncCommand(...);
//  Fiber::Yield();  // 协程让出 CPU，线程继续干别的
//  当回调 CmdCb 被触发：
//  ctx->fctx->rpy.reset(RedisReplyClone(reply), freeReplyObject);
//  ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);  // 唤醒协程
//  协程恢复执行，继续从 get() 后面跑。
// 优点：线程不会被占用，切换代价小，可承载成千上万个等待任务。
//
// 所以也就是这么回事，对于执行异步redis命令的线程来说是立即返回的，这个线程等于是把执行命令的任务交给了redis服务器，
// 交代完任务就直接返回了，所以这个线程是不阻塞的，但是这种情况下有个问题，redis服务器在执行完这个命令后可能会set一些值，
// 如果恰好外部工作线程又要获取这次set的值，所以为了保证同步我们要在异步执行完命令后调用的回到函数中使用Muext+条件变量
// 这一组合或者其他方式通知当前工作线程命令执行完了，所以当前工作线程是阻塞的，但是如果我们要是用协程的话，在把redis命令
// 交给redis服务器后台执行时把协程挂起，等执行到回调函数(代表命令执行完了)时，唤醒下当前协程，继续执行
//一句话总结就是：异步 Redis 命令不会阻塞线程，但用户需要“同步”时可以用条件变量阻塞线程，或者用协程挂起当前逻辑而不阻塞线程，从而获得同步的编码风格和高并发的运行效率。
void FoxRedis::CmdCb(redisAsyncContext *ac, void *r, void *privdata) {
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if(!ctx) {
        return;
    }
    if(ctx->timeout) {
        delete ctx;
        return;
    }
    auto m_logEnable = ctx->rds->m_logEnable;
    redisReply* reply = (redisReply*)r;
    if(ac->err) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "(" << ac->err << ") " << ac->errstr;
        }
        if(ctx->fctx->fiber) {
            //唤醒原协程 Fiber
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if(!reply) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: NULL";
        }
        if(ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if(reply->type == REDIS_REPLY_ERROR) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: " << reply->str;
        }
        if(ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else {
        if(ctx->fctx->fiber) {
            ctx->fctx->rpy.reset(RedisReplyClone(reply), freeReplyObject);
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    }
    ctx->cancelEvent();
    delete ctx;
}

//这个TimeCb每隔5s是否ping通的结果是在cmdCb中展示的，因为redisAsyncCommand函数执行完后会执行回调函数
void FoxRedis::TimeCb(int fd, short event, void* d) {
    FoxRedis* ar = static_cast<FoxRedis*>(d);
    redisAsyncCommand(ar->m_context.get(), CmdCb, nullptr, "ping");
}

//这部分释放逻辑syalr未实现好，后续自己实现
void FoxRedis::delayDelete(redisAsyncContext* c) {
    return;
}

bool FoxRedis::pinit() {
    // 如果已经不是未连接状态（可能正在连接或已连接），则不需要重复初始化，直接返回 true。
    if(m_status != UNCONNECTED) {
        return true;
    }

    // 使用异步方式连接 Redis 服务器，host 和 port 从成员变量中获取。
    auto ctx = redisAsyncConnect(m_host.c_str(), m_port);

    // 检查返回的连接上下文是否为 nullptr，表示连接创建失败，记录错误日志并返回 false。
    if(!ctx) {
        SYLAR_LOG_ERROR(g_logger) << "redisAsyncConnect (" << m_host << ":" << m_port
                    << ") null";
        return false;
    }

    if(ctx->err) {
        SYLAR_LOG_ERROR(g_logger) << "Error:(" << ctx->err << ")" << ctx->errstr;
        return false;
    }

    // 将当前对象的 this 指针绑定到 redisAsyncContext 中，供回调函数使用（如 ConnectCb、DisconnectCb）。
    ctx->data = this;

    // 将 Redis 的异步上下文绑定到 libevent 的事件循环中，使其能通过 event_base 驱动回调。
    redisLibeventAttach(ctx, m_thread->getBase());


    redisAsyncSetConnectCallback(ctx, ConnectCb);
    redisAsyncSetDisconnectCallback(ctx, DisconnectCb);
    m_status = CONNECTING;

    // 使用智能指针管理连接上下文，但不自动释放（nop 表示 no operation），
    // 防止在协程模型中释放时机不对导致的崩溃（如异步事件还未处理完）。
    m_context.reset(ctx, sylar::nop<redisAsyncContext>);

    // 注意：以下两种方式被注释了，表明在尝试其他释放策略：
    // 1. 自动释放（不适用于协程异步情况，容易导致崩溃）
    // m_context.reset(ctx, redisAsyncFree);
    // 2. 使用 delayDelete 延迟释放资源（被注释掉，可能暂不启用）
    // m_context.reset(ctx, std::bind(&FoxRedis::delayDelete, this, std::placeholders::_1));

    // 如果定时事件还未创建，则注册一个周期性定时事件（如心跳、超时检查等）。
    if(m_event == nullptr) {
        // 创建一个 libevent 定时器，120 秒触发一次，绑定 TimeCb 回调函数。
        m_event = event_new(m_thread->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        // 设置定时时间间隔为 120 秒。
        struct timeval tv = {120, 0};
        evtimer_add(m_event, &tv);  // 启动定时器。
    }

    // 主动触发一次定时器回调，确保初始化阶段能立即执行必要逻辑。
    TimeCb(0, 0, this);
    return true;
}


ReplyPtr FoxRedis::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxRedis::cmd(const char* fmt, va_list ap) {
    char* buf = nullptr;
    int len = redisvFormatCommand(&buf, fmt, ap);
    if(len == -1) {
        SYLAR_LOG_ERROR(g_logger) << "redis fmt error: " << fmt;
        return nullptr;
    }

    // 构造命令上下文 FCtx，用于在主协程和工作线程之间传递命令与结果
    FCtx fctx;
    // 将格式化后的命令字符串复制到 fctx.cmd 中，用于后续发送
    fctx.cmd.append(buf, len);
    free(buf);

    // 获取当前调度器（Scheduler），用于协程切换
    fctx.scheduler = sylar::Scheduler::GetThis();
    // 获取当前协程指针（Fiber），以便后续恢复执行
    fctx.fiber = sylar::Fiber::GetThis();
    // 将 pcmd 函数封装为任务提交给后台 Redis 线程执行（异步）
    m_thread->dispatch(std::bind(&FoxRedis::pcmd, this, &fctx));
    // 当前协程让出执行权，等待后台线程处理命令并唤醒协程
    sylar::Fiber::YieldToHold();
    // 返回后台线程填充的 Redis 响应结果
    return fctx.rpy;
}


ReplyPtr FoxRedis::cmd(const std::vector<std::string>& argv) {
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for(auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if(len == -1 || !buf) {
            SYLAR_LOG_ERROR(g_logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while(0);
    fctx.scheduler = sylar::Scheduler::GetThis();
    fctx.fiber = sylar::Fiber::GetThis();
    m_thread->dispatch(std::bind(&FoxRedis::pcmd, this, &fctx));
    sylar::Fiber::YieldToHold();
    return fctx.rpy;
}

void FoxRedis::pcmd(FCtx* fctx) {
    if(m_status == UNCONNECTED) {
        SYLAR_LOG_INFO(g_logger) << "redis (" << m_host << ":" << m_port << ") unconnected "
                   << fctx->cmd;
        // 尝试初始化 Redis 连接（异步连接）
        init();
        // 如果当前上下文中有关联的协程 fiber，则将其重新调度，
        // 意味着当前协程挂起，等待连接建立后再恢复执行
        if(fctx->fiber) {
            fctx->scheduler->schedule(&fctx->fiber);
        }
        return;  // 连接未建立，命令暂不发送，函数返回
    }

    // 连接已建立，创建新的请求上下文 Ctx，用于维护此次 Redis 命令的执行状态
    Ctx* ctx(new Ctx(this));

    // 保存当前调度线程指针，供后续调度使用
    ctx->thread = m_thread;
    ctx->init();
    // 关联外部传入的命令上下文，方便回调时使用
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;
    // 如果命令字符串非空，使用 hiredis 的异步接口发送格式化命令
    if(!ctx->cmd.empty()) {
        // 这里调用 redisAsyncFormattedCommand 发送命令
        // 参数说明：
        //  m_context.get()  - hiredis 异步上下文
        //  CmdCb           - 命令完成后的回调函数
        //  ctx             - 用户数据，回调时传回
        //  ctx->cmd.c_str() - 命令字符串指针
        //  ctx->cmd.size()  - 命令字符串长度
        redisAsyncFormattedCommand(m_context.get(), CmdCb, ctx, ctx->cmd.c_str(), ctx->cmd.size());
    }
}

FoxRedis::~FoxRedis() {
    if(m_event) {
        evtimer_del(m_event);
        event_free(m_event);
    }
}

//ctx封装一条命令的 libevent 超时处理和回调绑定
// 一个命令执行的完整链路：
// 发起 Redis 命令
// 调用 FoxRedis::pcmd(fctx)
// 此时命令被 异步发送，协程 挂起等待结果。
// 两种情况唤醒协程：
// 情况1：正常响应 Redis 异步返回，触发 CmdCb 回调：
// 情况2：超时,触发Ctx的EventCb
// [协程 A]
//    |
//    |--> 发起命令 --> FCtx（保存 fiber）
//    |                     |
//    |--> 创建 Ctx （绑定 FCtx + 设置超时事件）
//    |                     |
//    |--> hiredis 投递异步命令 + 回调绑定到 Ctx
//    |
//   yield() 协程挂起
//    |
//    +---------------------------+
//    |                           |
// [CmdCb 回调]             [EventCb 超时]
//    |                           |
//    |--> 恢复 FCtx->fiber      |--> 恢复 FCtx->fiber
//    +---------------------------+

FoxRedis::Ctx::Ctx(FoxRedis* r)
    : ev(nullptr)            // 超时事件初始化为空
    , timeout(false)         // 初始状态未超时
    , rds(r)                 // 保存 Redis 实例指针
    , thread(nullptr) {      // 所在线程指针初始化为空
    sylar::Atomic::addFetch(rds->m_ctxCount, 1); // 原子增加上下文引用计数
}

FoxRedis::Ctx::~Ctx() {
    SYLAR_ASSERT(thread == sylar::FoxThread::GetThis()); // 确保析构在正确线程中调用
    sylar::Atomic::subFetch(rds->m_ctxCount, 1); // 原子减少上下文引用计数

    if(ev) {
        evtimer_del(ev);   // 从 event_base 中移除事件
        event_free(ev);    // 释放 libevent 分配的事件资源
        ev = nullptr;
    }
}

void FoxRedis::Ctx::cancelEvent() {
}

bool FoxRedis::Ctx::init() {
    // 创建定时器事件，绑定回调 EventCb，传入 this 作为参数
    ev = evtimer_new(rds->m_thread->getBase(), EventCb, this);

    // 添加事件，等待 rds->m_cmdTimeout 指定的时间后触发
    evtimer_add(ev, &rds->m_cmdTimeout);
    return true;
}

void FoxRedis::Ctx::EventCb(int fd, short event, void* d) {
    Ctx* ctx = static_cast<Ctx*>(d); 
    ctx->timeout = 1;               
    if(ctx->rds->m_logEnable) {
        sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
        SYLAR_LOG_INFO(g_logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
            << (ctx->rds->m_cmdTimeout.tv_sec * 1000 +
                ctx->rds->m_cmdTimeout.tv_usec / 1000) << "ms";
    }

    // 若绑定了协程，则调度协程重新执行（唤醒等待中的 Redis 命令协程）
    if(ctx->fctx->fiber) {
        ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
    }
    ctx->cancelEvent();
}


FoxRedisCluster::FoxRedisCluster(sylar::FoxThread* thr, const std::map<std::string, std::string>& conf)
    :m_thread(thr)
    ,m_status(UNCONNECTED)
    ,m_event(nullptr) {
    m_ctxCount = 0;

    m_type = IRedis::FOX_REDIS_CLUSTER;
    m_host = get_value(conf, "host");
    m_passwd = get_value(conf, "passwd");
    m_logEnable = sylar::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = sylar::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

void FoxRedisCluster::OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv) {
    FoxRedisCluster* fr = (FoxRedisCluster*)priv;
    redisReply* r = (redisReply*)rp;
    if(!r) {
        SYLAR_LOG_ERROR(g_logger) << "auth error:("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(r->type != REDIS_REPLY_STATUS) {
        SYLAR_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(!r->str) {
        SYLAR_LOG_ERROR(g_logger) << "auth reply str error: NULL("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(strcmp(r->str, "OK") == 0) {
        SYLAR_LOG_INFO(g_logger) << "auth ok: " << r->str << "("
            << fr->m_host << ", " << fr->m_name << ")";
    } else {
        SYLAR_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
            << fr->m_host << ", " << fr->m_name << ")";
    }
}

void FoxRedisCluster::ConnectCb(const redisAsyncContext* c, int status) {
    FoxRedisCluster* ar = static_cast<FoxRedisCluster*>(c->data);
    if(!status) {
        SYLAR_LOG_INFO(g_logger) << "FoxRedisCluster::ConnectCb "
                   << c->c.tcp.host << ":" << c->c.tcp.port
                   << " success";
        if(!ar->m_passwd.empty()) {
            int rt = redisClusterAsyncCommand(ar->m_context.get(), FoxRedisCluster::OnAuthCb, ar, "auth %s", ar->m_passwd.c_str());
            if(rt) {
                SYLAR_LOG_ERROR(g_logger) << "FoxRedisCluster Auth fail: " << rt;
            }
        }
    } else {
        SYLAR_LOG_ERROR(g_logger) << "FoxRedisCluster::ConnectCb "
                    << c->c.tcp.host << ":" << c->c.tcp.port
                    << " fail, error:" << c->errstr;
    }
}

void FoxRedisCluster::DisconnectCb(const redisAsyncContext* c, int status) {
    SYLAR_LOG_INFO(g_logger) << "FoxRedisCluster::DisconnectCb "
               << c->c.tcp.host << ":" << c->c.tcp.port
               << " status:" << status;
}

void FoxRedisCluster::CmdCb(redisClusterAsyncContext *ac, void *r, void *privdata) {
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if(ctx->timeout) {
        delete ctx;
        return;
    }
    auto m_logEnable = ctx->rds->m_logEnable;
    redisReply* reply = (redisReply*)r;
    if(ac->err) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "(" << ac->err << ") " << ac->errstr;
        }
        if(ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if(!reply) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: NULL";
        }
        if(ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if(reply->type == REDIS_REPLY_ERROR) {
        if(m_logEnable) {
            sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
            SYLAR_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: " << reply->str;
        }
        if(ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else {
        if(ctx->fctx->fiber) {
            ctx->fctx->rpy.reset(RedisReplyClone(reply), freeReplyObject);
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    }
    delete ctx;
}

void FoxRedisCluster::TimeCb(int fd, short event, void* d) {
}

bool FoxRedisCluster::init() {
    if(m_thread == sylar::FoxThread::GetThis()) {
        return pinit();
    } else {
        m_thread->dispatch(std::bind(&FoxRedisCluster::pinit, this));
    }
    return true;
}

void FoxRedisCluster::delayDelete(redisAsyncContext* c) {
}

bool FoxRedisCluster::pinit() {
    if(m_status != UNCONNECTED) {
        return true;
    }
    SYLAR_LOG_INFO(g_logger) << "FoxRedisCluster pinit:" << m_host;
    auto ctx = redisClusterAsyncConnect(m_host.c_str(), 0);
    ctx->data = this;
    redisClusterLibeventAttach(ctx, m_thread->getBase());
    redisClusterAsyncSetConnectCallback(ctx, ConnectCb);
    redisClusterAsyncSetDisconnectCallback(ctx, DisconnectCb);
    if(!ctx) {
        SYLAR_LOG_ERROR(g_logger) << "redisClusterAsyncConnect (" << m_host
                    << ") null";
        return false;
    }
    if(ctx->err) {
        SYLAR_LOG_ERROR(g_logger) << "Error:(" << ctx->err << ")" << ctx->errstr
            << " passwd=" << m_passwd;
        return false;
    }
    m_status = CONNECTED;
    m_context.reset(ctx, sylar::nop<redisClusterAsyncContext>);
    if(m_event == nullptr) {
        m_event = event_new(m_thread->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        struct timeval tv = {120, 0};
        evtimer_add(m_event, &tv);
        TimeCb(0, 0, this);
    }
    return true;
}

ReplyPtr FoxRedisCluster::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxRedisCluster::cmd(const char* fmt, va_list ap) {
    char* buf = nullptr;
    int len = redisvFormatCommand(&buf, fmt, ap);
    if(len == -1 || !buf) {
        SYLAR_LOG_ERROR(g_logger) << "redis fmt error: " << fmt;
        return nullptr;
    }
    FCtx fctx;
    fctx.cmd.append(buf, len);
    free(buf);
    fctx.scheduler = sylar::Scheduler::GetThis();
    fctx.fiber = sylar::Fiber::GetThis();

    m_thread->dispatch(std::bind(&FoxRedisCluster::pcmd, this, &fctx));
    sylar::Fiber::YieldToHold();
    return fctx.rpy;
}

ReplyPtr FoxRedisCluster::cmd(const std::vector<std::string>& argv) {
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for(auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if(len == -1 || !buf) {
            SYLAR_LOG_ERROR(g_logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while(0);

    fctx.scheduler = sylar::Scheduler::GetThis();
    fctx.fiber = sylar::Fiber::GetThis();

    m_thread->dispatch(std::bind(&FoxRedisCluster::pcmd, this, &fctx));
    sylar::Fiber::YieldToHold();
    return fctx.rpy;
}

void FoxRedisCluster::pcmd(FCtx* fctx) {
    if(m_status != CONNECTED) {
        SYLAR_LOG_INFO(g_logger) << "redis (" << m_host << ") unconnected "
                   << fctx->cmd;
        init();
        if(fctx->fiber) {
            fctx->scheduler->schedule(&fctx->fiber);
        }
        return;
    }
    Ctx* ctx(new Ctx(this));
    ctx->thread = m_thread;
    ctx->init();
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;
    if(!ctx->cmd.empty()) {
        redisClusterAsyncFormattedCommand(m_context.get(), CmdCb, ctx, &ctx->cmd[0], ctx->cmd.size());
    }
}

FoxRedisCluster::~FoxRedisCluster() {
    if(m_event) {
        evtimer_del(m_event);
        event_free(m_event);
    }
}

FoxRedisCluster::Ctx::Ctx(FoxRedisCluster* r)
    :ev(nullptr)
    ,timeout(false)
    ,rds(r)
    ,thread(nullptr) {
    fctx = nullptr;
    sylar::Atomic::addFetch(rds->m_ctxCount, 1);
}

FoxRedisCluster::Ctx::~Ctx() {
    SYLAR_ASSERT(thread == sylar::FoxThread::GetThis());
    sylar::Atomic::subFetch(rds->m_ctxCount, 1);

    if(ev) {
        evtimer_del(ev);
        event_free(ev);
        ev = nullptr;
    }
}

void FoxRedisCluster::Ctx::cancelEvent() {
}

bool FoxRedisCluster::Ctx::init() {
    SYLAR_ASSERT(thread == sylar::FoxThread::GetThis());
    ev = evtimer_new(rds->m_thread->getBase(), EventCb, this);
    evtimer_add(ev, &rds->m_cmdTimeout);
    return true;
}

void FoxRedisCluster::Ctx::EventCb(int fd, short event, void* d) {
    Ctx* ctx = static_cast<Ctx*>(d);
    if(!ctx->ev) {
        return;
    }
    ctx->timeout = 1;
    if(ctx->rds->m_logEnable) {
        sylar::replace(ctx->cmd, "\r\n", "\\r\\n");
        SYLAR_LOG_INFO(g_logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
                   << (ctx->rds->m_cmdTimeout.tv_sec * 1000
                           + ctx->rds->m_cmdTimeout.tv_usec / 1000) << "ms";
    }
    ctx->cancelEvent();
    if(ctx->fctx->fiber) {
        ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
    }
}

IRedis::ptr RedisManager::get(const std::string& name) {
    sylar::RWMutex::WriteLock lock(m_mutex);
    auto it = m_datas.find(name);
    if(it == m_datas.end()) {
        return nullptr;
    }
    if(it->second.empty()) {
        return nullptr;
    }
    auto r = it->second.front();
    it->second.pop_front();
    // 判断是否为线程安全的 Redis 类型（FoxRedis / FoxRedisCluster）
    if(r->getType() == IRedis::FOX_REDIS
            || r->getType() == IRedis::FOX_REDIS_CLUSTER) {
        // 线程安全连接不需要独占使用，立刻放回池中可供复用
        it->second.push_back(r);
        // 返回一个 shared_ptr，使用空 deleter，防止自动 delete
        // 表示这个对象由池管理，调用方不能销毁，只是临时借用
        return std::shared_ptr<IRedis>(r, sylar::nop<IRedis>);
    }

    //非线程安全的 Redis（即同步 Redis），需要独占使用，释放锁避免死锁
    lock.unlock();
    // 尝试进行健康检查
    auto rr = dynamic_cast<ISyncRedis*>(r);
    if((time(0) - rr->getLastActiveTime()) > 30) {
        // 如果超过 30 秒没活动，执行 ping 检测连接是否健康
        if(!rr->cmd("ping")) {
            if(!rr->reconnect()) {
                // 重连失败，将连接重新放回池中
                sylar::RWMutex::WriteLock lock(m_mutex);
                m_datas[name].push_back(r);
                return nullptr;
            }
        }
    }
    // 更新活跃时间
    rr->setLastActiveTime(time(0));
    // 返回一个 shared_ptr，传入自定义 deleter，
    // 用于在引用计数归零时将连接归还给连接池
    return std::shared_ptr<IRedis>(r, std::bind(&RedisManager::freeRedis,
                        this, std::placeholders::_1));
}

void RedisManager::freeRedis(IRedis* r) {
    sylar::RWMutex::WriteLock lock(m_mutex);
    m_datas[r->getName()].push_back(r);
}

RedisManager::RedisManager() {
    init();
}

void RedisManager::init() {
    m_config = g_redis->getValue();
    size_t done = 0;   // 已完成初始化的实例数
    size_t total = 0;  // 所有需要初始化的实例总数

    for(auto& i : m_config) {
        auto type = get_value(i.second, "type");           
        auto pool = sylar::TypeUtil::Atoi(get_value(i.second, "pool")); // 获取连接池数量（多少个实例）
        auto passwd = get_value(i.second, "passwd"); 

        total += pool; // 总数加上当前配置的连接池大小
        for(int n = 0; n < pool; ++n) {
            if(type == "redis") {
                sylar::Redis* rds(new sylar::Redis(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                sylar::RWMutex::WriteLock lock(m_mutex);
                m_datas[i.first].push_back(rds); // 加入连接池中
                sylar::Atomic::addFetch(done, 1); // 原子地记录完成数 +1

            } else if(type == "redis_cluster") {
                sylar::RedisCluster* rds(new sylar::RedisCluster(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                sylar::RWMutex::WriteLock lock(m_mutex);
                m_datas[i.first].push_back(rds);
                sylar::Atomic::addFetch(done, 1);

            } else if(type == "fox_redis") {
                // 异步 FoxRedis，通过 FoxThread 分发到 Redis 线程执行（必须线程绑定）
                auto conf = i.second;
                auto name = i.first;
                //dispatch("redis", callback) 会把这个 callback 分发（投递）给它内部的某个“redis”线程池中的线程去执行。
                sylar::FoxThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, &done](){
                    // 在线程回调中初始化 FoxRedis 对象，绑定当前线程
                    sylar::FoxRedis* rds(new sylar::FoxRedis(sylar::FoxThread::GetThis(), conf));
                    rds->init();    
                    rds->setName(name);
                    sylar::RWMutex::WriteLock lock(m_mutex);
                    m_datas[name].push_back(rds); 
                    sylar::Atomic::addFetch(done, 1); 
                });

            } else if(type == "fox_redis_cluster") {
                // 异步 FoxRedisCluster，绑定线程，原理与 fox_redis 相同
                auto conf = i.second;
                auto name = i.first;
                sylar::FoxThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, &done](){
                    sylar::FoxRedisCluster* rds(new sylar::FoxRedisCluster(sylar::FoxThread::GetThis(), conf));
                    rds->init();
                    rds->setName(name);
                    sylar::RWMutex::WriteLock lock(m_mutex);
                    m_datas[name].push_back(rds);
                    sylar::Atomic::addFetch(done, 1);
                });
            } else {
                // 如果是未知类型，也计数，以免卡住等待逻辑
                sylar::Atomic::addFetch(done, 1);
            }
        }
    }
    // 等待所有 Redis 实例初始化完成（同步/异步都计入）
    while(done != total) {
        usleep(5000); // 睡眠 5 毫秒避免忙等
    }
}

std::ostream& RedisManager::dump(std::ostream& os) {
    os << "[RedisManager total=" << m_config.size() << "]" << std::endl;
    for(auto& i : m_config) {
        os << "    " << i.first << " :[";
        for(auto& n : i.second) {
            os << "{" << n.first << ":" << n.second << "}";
        }
        os << "]" << std::endl;
    }
    return os;
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = Cmd(name, fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const char* fmt, va_list ap) {
    auto rds = RedisMgr::GetInstance()->get(name);
    if(!rds) {
        return nullptr;
    }
    return rds->cmd(fmt, ap);
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const std::vector<std::string>& args) {
    auto rds = RedisMgr::GetInstance()->get(name);
    if(!rds) {
        return nullptr;
    }
    return rds->cmd(args);
}

ReplyPtr RedisUtil::TryCmd(const std::string& name, uint32_t count, const char* fmt, ...) {
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, fmt);
        ReplyPtr rt = Cmd(name, fmt, ap);
        va_end(ap);

        if(rt) {
            return rt;
        }
    }
    return nullptr;
}

ReplyPtr RedisUtil::TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args) {
    for(uint32_t i = 0; i < count; ++i) {
        ReplyPtr rt = Cmd(name, args);
        if(rt) {
            return rt;
        }
    }
    return nullptr;
}

}