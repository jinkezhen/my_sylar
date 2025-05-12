/**
 * @file fox_thread.h
 * @brief 实现和管理线程池和线程调度
 * @date 2025-05-11
 * @copyright Copyright (c) 2025 All rights reserved.
 */

#ifndef __SYLAR_DB_FOX_THREAD_H
#define __SYLAT_DB_FOX_THREAD_H

#include <thread>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <functional>
#include <memory>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>

#include "sylar/singleton.h"
#include "sylar/mutex.h"

        //                      +----------------------+
        //                      |      IFoxThread      |  ← 抽象线程接口
        //                      |----------------------|
        //                      | + dispatch()         |
        //                      | + start()            |
        //                      | + stop()             |
        //                      | + ...                |
        //                      +----------^-----------+
        //                                 |
        //             --------------------|----------------------
        //             |                                         |
        // +------------------------+              +---------------------------+
        // |       FoxThread        |              |      FoxThreadPool        |
        // |------------------------|              |---------------------------|
        // | 单个线程 + event_loop  |               | 多线程池 + 回调分发机制     |
        // | 内部维护 thread/event  |               | 维护线程列表及任务队列      |
        // | 支持任务 dispatch     |                | 支持负载均衡分发任务        |
        // +------------------------+              +---------------------------+
        //             ^                                         ^
        //             |                                         |
        //             |               +-------------------------+
        //             |               |
        // +-----------------------------------+
        // |        FoxThreadManager           |
        // |-----------------------------------|
        // |  线程调度管理器，支持按名称查找    |
        // |  维护 map<string, IFoxThread::ptr>|
        // |  可分发/广播任务至指定线程或池中   |
        // |  start()/stop() 控制所有子线程     |
        // +-----------------------------------+

namespace sylar {

class FoxThread;
// 这个 IFoxThread 类是一个线程调度接口类，属于 Sylar 框架中的事件线程调度模块，
// 它为不同的线程模型（如单线程、线程池）提供统一的调度方式。IFoxThread 是一个
// 纯虚类，定义了一组线程调度的接口，供其派生类（如 FoxThread、FoxThreadPool）实现。
// IFoxThread 用于抽象线程调度操作，统一事件派发、线程控制和状态输出的接口，
// 让用户无需关心线程模型的具体实现，即可对任务进行分发、广播等操作。
class IFoxThread {
public:
    typedef std::shared_ptr<IFoxThread> ptr;
    //被调度的任务
    typedef std::function<void()> callback;

    virtual ~IFoxThread() {};

    //将一个任务分发给某个线程(通常是随机或轮询方式选择)
    virtual bool dispatch(callback cb);
    //将任务分发到指定的ID的线程，支持线程定向调度
    virtual bool dispatch(uint32_t id, callback cb);
    //批量分发任务，通常用于高性能场景下一次性提交多个任务
    virtual bool batchDispatch(const std::vector<callback>& cbs);
    //广播任务，让所有线程都执行一次回调函数，如配置更新、通知等
    virtual void broadcast(callback cb);
    //启动线程或线程池
    virtual void start();
    //停止线程或线程池
    virtual void stop();
    //等待所有线程执行完成，用于程序退出前
    virtual void join();
    //输出线程状态到流
    virtual void dump(std::ostream& os);
    //返回线程池总共调度过的任务数量
    virtual uint64_t getTotal();
};

//FoxThread 类是一个专门用于多线程任务调度的类，它通过事件驱动模型实现高效的任务处理。它通过 libevent 库的事件循环机制，
//能够在后台不断监听和处理任务。每个 FoxThread 实例都代表一个独立的工作线程，负责执行分发到该线程的任务。通过提供灵活的
//任务调度接口，FoxThread 允许外部线程将任务分发到当前线程，并确保任务的顺序执行和线程安全。
//实现上，FoxThread 利用了 UNIX socketpair 管道机制实现线程间的通信，通过 m_read 和 m_write 套接字监听任务的到来。
//当任务通过 dispatch() 或 batchDispatch() 等接口被派发时，FoxThread 会将这些任务加入到任务队列中，并通过 m_write 
//通知线程执行任务。在事件循环中，FoxThread 会持续等待来自 m_read 的可读事件，一旦有任务被写入，它就会触发回调函数并
//处理任务。通过这种方式，FoxThread 实现了任务的异步调度和高效处理，适用于高并发的多线程应用。
class FoxThread : public IFoxThread {
public:
    typedef std::shared_ptr<FoxThread> ptr;
    typedef IFoxThread::callback callback;
    //初始化回调函数的函数
    typedef std::function<void(FoxThread*)> init_cb;

    //name：该线程的名字
    //base：事件循环的上下文
    //事件循环：持续等待事件并处理事件的循环机制，本质是这样一种逻辑
    // while (running) {
    //     等待某个事件（网络、定时器、信号、通知等）；
    //     有事件发生就分发给对应的处理回调函数；
    // }
    // 在 libevent 中，这个循环是由 event_base_dispatch() 或 event_base_loop() 来运行的，背后由 event_base* 这个结构管理。
    FoxThread(const std::string& name, struct event_base* base = NULL);
    ~FoxThread();

    //获取的当前线程关联的FoxThread实例
    static FoxThread* GetThis();
    static const std::string& GetFoxThreadName();
    //程序可能创建多个 FoxThread 实例，GetAllFoxThreadName() 允许开发者一次性获取所有线程的名称
    //存放threadId----threadName
    static void GetAllFoxThreadName(std::map<uint64_t, std::string>& name);

    //将当前 FoxThread 实例绑定到线程局部存储中（例如 TLS）。
    void setThis();
    //解绑当前线程中的 FoxThread 实例。
    void unsetThis();

    bool dispatch(callback cb) override;
    bool dispatch(uint32_t id, callback cb) override;
    bool batchDispatch(const std::vector<callback>& cbs) override;
    void broadcast(callback cb) override;

    void start() override;
    void stop() override;
    void join() override;
    void dump(std::ostream& os) override;
    uint64_t getTotal() override;

    //检查线程是否已经启动
    bool isStart() const;

    //获取事件基础结构
    struct event_base* getBase();
    //获取当前线程的id
    std::thread::id getId() const;

    //获取某个名字下绑定的私有数据
    void* getData(const std::string& name);
    template<typename T>
    T* getData(const std::string& name);
    //在线程中存储私有数据
    void setData(const std::string& name, void* v);

    //初始化回调
    void setInitCb(init_cb v);

private:
    //FoxThread的主线程循环函数，由std::thread启动时执行
    //thread_cb() 是 线程主函数，它负责：
    // 初始化 event_base（即事件循环上下文）；
    // 创建事件监听器（监听 m_read）；
    // 将 read_cb 注册为 m_read 的回调函数；
    // 启动事件循环（通过 event_base_dispatch()）；
    void thread_cb();
    //与事件系统绑定的回调函数，当m_read可读时(说明主线程发来了任务)触发
    //从任务队列中取出任务，并顺序执行
    //触发条件：
    //主线程通过 m_write 套接字写入任意内容时，会触发 m_read 的可读事件，从而执行 read_cb。
    static void read_cb(evutil_socket_t sock, short which, void* args);

private:
    //这两个成员是UNIX socketpair管道的两个端点，用于线程间通信
    evutil_socket_t m_read; //在FoxThread所在线程中监听此管道，一旦有数据读入，就唤醒event loop执行任务
    evutil_socket_t m_write;//在主线程或外部线程调用dispatch时使用，把任务写入通道

    //libevent的"事件循环"核心结构，代表一个事件多路复用的运行环境
    //负责
    // 管理所有注册的 event（比如监听 m_read）；
    // 负责阻塞等待 IO 事件并触发回调；
    // 类似于 epoll_wait() 背后的调度器。
    struct event_base* m_base;

    //代表绑定到m_read管道上的读事件
    // 会设置成“可读时回调 read_cb()”；
    // 一旦有任务被 dispatch() 派发到当前线程，就通过 m_write 写入一个字节唤醒这个事件。
    struct event* m_event;

    //该FoxThread实际运行的工作线程
    //当这个线程被启动时，它运行的主函数是thread_cb()
    std::thread* m_thread;

    //保护共享资源
    sylar::RWMutex m_mutex;

    //该任务队列来存放所有通过dispatch()等接口派发到该线程的回调函数
    std::list<callback> m_callbacks;

    //线程的名称
    std::string m_name;

    //初始化回调函数
    //在thread_cb启动前调用
    //通常用于给当前线程初始化上下文、设置线程局部变量等。
    init_cb m_initCb;

    //标记当前线程是否在处理任务的状态标志
    bool m_working;
    //标志线程是否已经启动
    bool m_start;
    //记录当前线程累计处理的任务数量，便于监控、统计用途。
    uint64_t m_total;

    //为当前FoxThread绑定一些自定义的数据对象
    std::map<std::string, void*> m_datas;
};

//FoxThreadPool 类是一个多线程池的实现，旨在通过预先创建多个线程来高效地处理任务。它通过对多个 FoxThread 实例的管理，
// 使得任务能够被快速调度到空闲的线程中，从而提升了任务处理的效率。在任务调度时，FoxThreadPool 会根据当前的线程池状态，
// 决定是将任务随机分配到某个线程，还是指定某个线程来处理任务。同时，它还支持批量任务调度，能够一次性将多个任务分配给线程
// 池中的线程进行处理。
//在实现上，FoxThreadPool 维护了一个 FoxThread 对象的列表，并通过 m_freeFoxThreads 列表来管理空闲的线程。
// 任务被添加到一个等待队列中，当有线程空闲时，它会取出任务并执行。FoxThreadPool 还支持线程池的初始化回调，
// 使得每个线程在启动时执行特定的初始化操作。通过这些机制，FoxThreadPool 可以高效地复用线程、调度任务，
// 并在任务负载增加时动态处理任务分发。
class FoxThreadPool : public IFoxThread {
public:
    typedef std::shared_ptr<FoxThreadPool> ptr;
    typedef IFoxThread::callback callback; 
    
    //size：线程池中要创建多少个线程
    //name：线程池的名字
    //advance：是否使用高级模式/调度增强模式
    FoxThreadPool(uint32_t size, const std::string& name = "", bool advance = false);
    ~FoxThreadPool();

        //随机线程执行
    bool dispatch(callback cb) override;
    bool batchDispatch(const std::vector<callback>& cb) override;
    //指定线程执行
    bool dispatch(uint32_t id, callback cb) override;

    //从线程列表中随机获取一个线程
    FoxThread* getRandFoxThread();
    //指定线程启动时要执行的初始化动作
    void setInitCb(FoxThread::init_cb v) { m_initCb = v;}

    void start() override;
    void stop() override;
    void join() override;
    void dump(std::ostream& os) override;
    void broadcast(callback cb) override;
    uint64_t getTotal() override { return m_total;}

private:
    //回收线程
    void releaseFoxThread(FoxThread* t);
    //其作用通常是检查有没有空闲线程、是否该唤醒线程来处理等待队列中的任务。
    void check();
    // 包装任务回调，增加额外逻辑
    void wrapcb(std::shared_ptr<FoxThread>, callback cb);

private:
    //线程池中的线程数量，构造时指定
    uint32_t m_size;
    //当前分发用的线程索引，用于轮询分发任务
    uint32_t m_cur;
    //线程池的名字
    std::string m_name;
    bool m_advance;
    bool m_start;
    RWMutex m_mutex;
    //等待执行的任务列表，当前没有空闲线程时可临时存放任务
    std::list<callback> m_callbacks;
    std::vector<FoxThread*> m_threads;
    //当前空闲可调度的线程集合，用于高效复用线程
    std::list<FoxThread*> m_freeFoxThreads;
    //线程初始化时要执行的回调函数
    FoxThread::init_cb m_initCb;
    //已调度任务的总数
    uint64_t m_total;
};

//FoxThreadManager 类是一个管理多个线程池和线程实例的中央管理器。它提供了对不同线程池的任务调度、状态管理和资源控制功能。
// 通过这个类，开发者可以方便地向指定线程池派发任务，不仅支持单个任务的分配，还支持批量任务和广播任务，极大地简化了多线程任
// 务管理的复杂度。此外，FoxThreadManager 还可以将线程池的状态信息打印出来，便于监控和调试系统的运行状态。它的核心作用是
// 将线程池和线程实例按照名字进行管理，并提供简单的接口进行操作。
//实现上，FoxThreadManager 使用一个 std::map 数据结构来维护线程池或线程对象与其对应的名字之间的映射关系。通过这种映射，
// 管理器可以快速定位到指定的线程池或线程实例，并对其进行任务调度、状态打印等操作。在初始化和启动时，管理器会启动所有注册
// 的线程池或线程对象，停止时则会释放资源。FoxThreadManager 作为一个全局单例，确保了全系统范围内的线程管理和任务调度的
// 统一性和高效性。
class FoxThreadManager {
public:
    typedef IFoxThread::callback callback;

    //向名为name的线程池中随机选择一个线程派发任务。
    void dispatch(const std::string& name, callback cb);
    //向名为name的线程池中的 指定线程 派发任务。
    void dispatch(const std::string& name, uint32_t id, callback cb);
    // 向名为name的线程池中 批量派发多个任务。
    void batchDispatch(const std::string& name, const std::vector<callback>& cbs);
    // 向指定名字的线程池中的 所有线程广播执行同一个任务回调。
    void broadcast(const std::string& name, callback cb);
    // 将所有线程池或线程对象的状态信息打印到输出流。
    void dumpFoxThreadStatus(std::ostream& os);
    // 初始化线程管理器
    void init();
    // 启动所有注册进来的线程池或线程对象。
    void start();
    //停止所有注册的线程池或线程对象，释放资源。
    void stop() ;
    // 获取指定名称的线程池/线程实例。
    IFoxThread::ptr get(const std::string& name);
    // 向管理器中注册一个线程池或线程对象。
    void add(const std::string& name, IFoxThread::ptr thr);

private:
    //维护一个名字到线程/线程池之间的映射
    std::map<std::string, IFoxThread::ptr> m_threads;
};

typedef sylar::Singleton<FoxThreadManager> FoxThreadMgr;

}

#endif