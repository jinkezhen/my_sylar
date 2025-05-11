#include "fox_thread.h"
#include "sylar/config.h"
#include "sylar/log.h"
#include "sylar/util.h"
#include "sylar/macro.h"
#include "sylar/config.h"
#include <iomanip>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static sylar::ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_thread_info_set
            = Config::Lookup("fox_thread", std::map<std::string, std::map<std::string, std::string> >()
                    ,"confg for thread");

//全局静态变量，用于跨多个 FoxThread 实例共享数据
static RWMutex s_thread_mutex;
static std::map<uint64_t, std::string> s_thread_names;
//线程局部变量，每个线程都有自己独立一份s_thread指针
//我们可以通过FoxThread::GetThis()拿到当前线程的FoxThread*实例
thread_local FoxThread* s_thread = nullptr;

void FoxThread::read_cb(evutil_socket_t sock, short which, void* args) {
    FoxThread* thread = static_cast<FoxThread*>(args);
    uint8_t cmd[4096];
    if (recv(sock, cmd, sizeof(cmd), 0) > 0) {
        std::list<callback> callbacks;
        RWMutex::WriteLock lock(thread->m_mutex);
        callbacks.swap(thread->m_callbacks);
        lock.unlock();
        thread->m_working = true;
        //auto：std::list<std::function<void()>>::iterator
        for (std::list<std::function<void()>>::iterator it = callbacks.begin(); it != callbacks.end(); ++it) {
            if (*it) {
                try {
                    (*it)();
                }  catch (std::exception& ex) {
                    SYLAR_LOG_ERROR(g_logger) << "exception:" << ex.what();
                } catch (const char* c) {
                    SYLAR_LOG_ERROR(g_logger) << "exception:" << c;
                } catch (...) {
                    SYLAR_LOG_ERROR(g_logger) << "uncatch exception";
                }
            } else {
                //如果回调为空，说明要停止线程
                //中断事件循环，让线程退出
                event_base_loopbreak(thread->m_base);
                thread->m_start = false;
                //清空线程局部存储
                thread->unsetThis();
                break;
            }
        }
        sylar::Atomic::addFetch(thread->m_total, callbacks.size());
    }
}

FoxThread::FoxThread(const std::string& name, struct event_base* base)
    :m_read(0)
    ,m_write(0)
    ,m_base(NULL)
    ,m_event(NULL)
    ,m_thread(NULL)
    ,m_name(name)
    ,m_working(false)
    ,m_start(false)
    ,m_total(0) {
    int fds[2];
    if(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        throw std::logic_error("thread init error");
    }
    //设置套接字为非阻塞
    // 通过设置 m_read 和 m_write 为非阻塞套接字，FoxThread 可以保持高效、
    // 响应迅速的事件循环。它不必在等待任务时被阻塞，而是可以继续执行其他任务
    // 或周期性检查是否有新的任务。这种设计使得 FoxThread 能够以事件驱动的方
    // 式处理大量任务，并确保不会因为 I/O 操作的延迟而影响到整体的线程性能。
    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    m_read = fds[0];
    m_write = fds[1];

    if (base) {
        //当传入外部event_base*，意味着当前线程通常是主线程，将直接使用FoxThread的事件循环
        //主线程负责初始化 FoxThreadManager 和线程池，启动和管理所有子线程的生命周期（创建、启动、停止、回收等）。
        //而start中的子线程用来通过 libevent 事件循环等待并处理分发过来的任务（callback），执行异步任务逻辑。
        m_base = base;
        //将当前FoxThread绑定到线程
        setThis();
    } else {
        m_base = event_base_new();
    }
    //创建事件对象：
    // 使用 event_new 函数为 m_read（接收端套接字）创建一个新的事件。
    // EV_READ 表示当套接字可读时触发该事件。
    // EV_PERSIST 表示事件持续存在，即在事件触发后仍然有效。
    // read_cb 是触发事件时调用的回调函数，this 作为参数传递给回调函数。
    m_event = event_new(m_base, m_read, EV_READ | EV_PERSIST, read_cb, this);
    //添加事件到事件循环
    event_add(m_event, NULL);
}

void FoxThread::dump(std::ostream& os) {
    RWMutex::ReadLock lock(m_mutex);
    os << "[thread name=" << m_name
       << " working=" << m_working
       << " tasks=" << m_callbacks.size()
       << " total=" << m_total
       << "]" << std::endl;
}

std::thread::id FoxThread::getId() const {
    if (m_thread) {
        return m_thread->get_id();
    }
    //返回一个默认构造的id对象
    return std::thread::id();
}

void* FoxThread::getData(const std::string& name) {
    auto it = m_datas.find(name);
    return it == m_datas.end() ? nullptr : it->second;
}

void  FoxThread::setData(const std::string& name, void* v) {
    //Mutex::WriteLock lock(m_mutex);
    m_datas[name] = v;
}

FoxThread::~FoxThread() {
    if (m_read) {
        close(m_read);
    }
    if (m_write) {
        close(m_write);
    }
    stop();
    join();
    if (m_thread) {
        delete m_thread;
    }
    if (m_event) {
        event_free(m_event);
    }
    if (m_base) {
        event_base_free(m_base);
    }
}

void FoxThread::start() {
    if (m_thread) {
        throw std::logic_error("FoxThread is running");
    }
    //start后会创建一个新的子线程
    //这个子线程的目的就是让它在后台一直处理异步事件
    //让这个线程进入一个“事件驱动循环”中，它阻塞的不是 I/O，
    //而是等待事件（如 socket 可读、超时、信号等）触发回调。
    m_thread = new std::thread(std::bind(&FoxThread::thread_cb, this));
    m_start = true;
}

void FoxThread::thread_cb() {
    setThis();
    //为当前线程设置名称
    pthread_setname_np(pthread_self(), m_name.substr(0,15).c_str());
    if (m_initCb) {
        //如果设置了初始化回调
        m_initCb(this);
        m_initCb = nullptr;
    }
    //启动libevent的事件循环，监听和处理事件。该线程将进入阻塞状态，持续等待任
    //务事件的触发并进行处理，直到事件循环被外部中止（如停止线程时）。
    event_base_loop(m_base, 0);
}

// 将一个回调任务添加到线程的任务队列中，并通过 socket 激活事件循环，使子线程去处理这个任务。
bool FoxThread::dispatch(callback cb) {
    RWMutex::WriteLock lock(m_mutex);
    m_callbacks.push_back(cb);
    lock.unlock();
    uint8_t cmd = 1;
    if (send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

bool FoxThread::dispatch(uint32_t id, callback cb) {
    return dispatch(cb);
}

bool FoxThread::batchDispatch(const std::vector<callback>& cbs) {
    RWMutex::WriteLock lock(m_mutex);
    for (auto i : cbs) {
        m_callbacks.push_back(i);
    }
    lock.unlock();
    uint8_t cmd = 1;
    if (send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

void FoxThread::broadcast(callback cb) {
    dispatch(cb);
}

void FoxThread::stop() {
    RWMutex::WriteLock lock(m_mutex);
    m_callbacks.push_back(nullptr);
    if (m_thread) {
        uint8_t cmd = 0;
        send(m_write, &cmd, sizeof(cmd), 0);
    }
}

void FoxThread::join() {
    if (m_thread) {
        m_thread->join();
        //delete 会释放堆内存，但不会把指针本身清空，指针仍然保存着原先的地址。如果之后不小心访问这个指针
        //（例如再次使用 *m_thread），就会访问已被释放的内存，行为是未定义的（undefined behavior），非常危险。
        //设置为 NULL/nullptr 后，访问就变成空指针访问，可以被程序快速识别和拦截
        //（很多平台下会直接崩溃在访问空指针的位置），更容易调试。
        delete m_thread;
        m_thread = NULL;
    }
}

FoxThread* FoxThread::GetThis() {
    return s_thread;
}

void FoxThread::setThis() {
    m_name = m_name + "_" + std::to_string(sylar::GetThreadId());
    s_thread = this;
    RWMutex::WriteLock lock(s_thread_mutex);
    s_thread_names[sylar::GetThreadId()] = m_name;
}

void FoxThread::unsetThis() {
    s_thread = nullptr;
    RWMutex::WriteLock lock(s_thread_mutex);
    s_thread_names.erase(sylar::GetThreadId());
}

const std::string& FoxThread::GetFoxThreadName() {
    FoxThread* t = GetThis();
    if (t) {
        return t->m_name;
    }
    uint64_t tid = sylar::GetThreadId();
    do {
        RWMutex::ReadLock lock(s_thread_mutex);
        auto it = s_thread_names.find(tid);
        if(it != s_thread_names.end()) {
            return it->second;
        }
    } while(0);

    do {
        RWMutex::WriteLock lock(s_thread_mutex);
        s_thread_names[tid] = "UNNAME_" + std::to_string(tid);
        return s_thread_names[tid];
    } while (0);
}

void FoxThread::GetAllFoxThreadName(std::map<uint64_t, std::string>& names) {
    RWMutex::WriteLock lock(s_thread_mutex);
    names.insert(s_thread_names.begin(), s_thread_names.end());
}

FoxThreadPool::FoxThreadPool(uint32_t size, const std::string& name, bool advance)
    :m_size(size)
    ,m_cur(0)
    ,m_name(name)
    ,m_advance(advance)
    ,m_start(false)
    ,m_total(0) {
    m_threads.resize(size);
    for (size_t i = 0; i < size; ++i) {
        FoxThread* t(new FoxThread(name + "_" + std::to_string(i)));
        m_threads[i] = t;
    }
}

FoxThreadPool::~FoxThreadPool() {
    for(size_t i = 0; i < m_size; ++i) {
        delete m_threads[i];
    }
}

void FoxThreadPool::start() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->setInitCb(m_initCb);
        m_threads[i]->start();
        m_freeFoxThreads.push_back(m_threads[i]);
    }
    if(m_initCb) {
        m_initCb = nullptr;
    }
    m_start = true;
    check();
}

void FoxThreadPool::stop() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->stop();
    }
    m_start = false;
}

void FoxThreadPool::join() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->join();
    }
}

void FoxThreadPool::releaseFoxThread(FoxThread* t) {
    {
        RWMutex::WriteLock lock(m_mutex);
        m_freeFoxThreads.push_back(t);
    }
    check();
}

bool FoxThreadPool::dispatch(callback cb) {
    {
        sylar::Atomic::addFetch(m_total, (uint64_t)1);
        RWMutex::WriteLock lock(m_mutex);
        if (!m_advance) {
            //如果不是高级调度模式，则直接将任务cb分发给某个线程，这是一种轮询调度策略，平均分到每个线程
            return m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        //如果是高级调度模式，则先将任务放入等待队列中，等待空闲线程来拉取任务
        m_callbacks.push_back(cb);
    }
    check();
    return true;
}

bool FoxThreadPool::batchDispatch(const std::vector<callback>& cbs) {
    sylar::Atomic::addFetch(m_total, cbs.size());
    RWMutex::WriteLock lock(m_mutex);
    if(!m_advance) {
        for(auto cb : cbs) {
            m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        return true;
    }
    for(auto cb : cbs) {
        m_callbacks.push_back(cb);
    }
    lock.unlock();
    check();
    return true;
}

void FoxThreadPool::wrapcb(std::shared_ptr<FoxThread> thr, callback cb) {
    cb();
}

bool FoxThreadPool::dispatch(uint32_t id, callback cb) {
    sylar::Atomic::addFetch(m_total, (uint64_t)1);
    return m_threads[id % m_size]->dispatch(cb);
}

FoxThread* FoxThreadPool::getRandFoxThread() {
    return m_threads[m_cur++ % m_size];
}

void FoxThreadPool::broadcast(callback cb) {
    for(size_t i = 0; i < m_threads.size(); ++i) {
        m_threads[i]->dispatch(cb);
    }
}

void FoxThreadPool::dump(std::ostream& os) {
    RWMutex::ReadLock lock(m_mutex);
    os << "[FoxThreadPool name = " << m_name << " thread_count = " << m_threads.size()
       << " tasks = " << m_callbacks.size() << " total = " << m_total
       << " advance = " << m_advance
       << "]" << std::endl;
    for(size_t i = 0; i < m_threads.size(); ++i) {
        os << "    ";
        m_threads[i]->dump(os);
    }
}           

// 检查是否有空闲线程和待处理的回调任务，
// 如果有，则将任务派发到线程中执行
void FoxThreadPool::check() {
    do {
        // 如果线程池尚未启动，直接退出
        if (!m_start) {
            break;
        }

        // 加写锁，确保对线程池内部数据结构的修改是线程安全的
        RWMutex::WriteLock lock(m_mutex);

        // 如果没有空闲线程或没有待处理的任务，直接退出
        if (m_freeFoxThreads.empty() || m_callbacks.empty()) {
            break;
        }

        // 从空闲线程列表中取出一个线程（注意用 shared_ptr 包装，带自定义 deleter）
        std::shared_ptr<FoxThread> thr(m_freeFoxThreads.front(), 
                                       std::bind(&FoxThreadPool::releaseFoxThread, this, std::placeholders::_1));
        m_freeFoxThreads.pop_front();

        // 取出一个待执行的回调任务
        callback cb = m_callbacks.front();
        m_callbacks.pop_front();

        // 解锁，因为后续执行任务派发可能较慢，不希望锁持有太久
        lock.unlock();

        // 如果线程已经启动，则派发任务（wrapcb 用于包装用户回调）
        if (thr->isStart()) {
            thr->dispatch(std::bind(&FoxThreadPool::wrapcb, this, thr, cb));
        } else {
            // 如果线程尚未启动，则将任务重新放回任务队列
            RWMutex::WriteLock lock(m_mutex);
            m_callbacks.push_front(cb);
        }
    } while (true); // 保证整个过程在可处理条件下至少执行一次，否则立即退出
}

IFoxThread::ptr FoxThreadManager::get(const std::string& name) {
    auto it = m_threads.find(name);
    return it == m_threads.end() ? nullptr : it->second;
}

void FoxThreadManager::add(const std::string& name, IFoxThread::ptr thr) {
    m_threads[name] = thr;
}

void FoxThreadManager::dispatch(const std::string& name, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->dispatch(cb);
}


void FoxThreadManager::dispatch(const std::string& name, uint32_t id, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->dispatch(id, cb);
}

void FoxThreadManager::batchDispatch(const std::string& name, const std::vector<callback>& cbs) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->batchDispatch(cbs);
}


void FoxThreadManager::broadcast(const std::string& name, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->broadcast(cb);
}


void FoxThreadManager::dumpFoxThreadStatus(std::ostream& os) {
    os << "FoxThreadManager: " << std::endl;
    for(auto it = m_threads.begin();
            it != m_threads.end(); ++it) {
        it->second->dump(os);
    }

    os << "All FoxThreads:" << std::endl;
    std::map<uint64_t, std::string> names;
    FoxThread::GetAllFoxThreadName(names);
    for(auto it = names.begin();
            it != names.end(); ++it) {
        os << std::setw(30) << it->first
           << ": " << it->second << std::endl;
    }
}

void FoxThreadManager::init() {
    auto m = g_thread_info_set->getValue();
    for (auto i : m) {
        //当前线程池需要的线程数
        auto num = sylar::GetParamValue(i.second, "num", 0);
        //线程或线程池的名
        auto name = i.first;
        auto advance = sylar::GetParamValue(i.second, "advance", 0);
        if(num <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "thread pool:" << name
                        << " num:" << num
                        << " advance:" << advance
                        << " invalid";
            continue;
        }
        if (num == 1) {
            m_threads[name] = FoxThread::ptr(new FoxThread(name));
        } else {
            m_threads[name] = FoxThreadPool::ptr(new FoxThreadPool(num, name, advance));
        }
    }
}

void FoxThreadManager::start() {
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " start begin";
        i.second->start();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " start end";
    }
}

void FoxThreadManager::stop() {
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " stop begin";
        i.second->stop();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " stop end";
    }
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " join begin";
        i.second->join();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " join end";
    }
} 

}