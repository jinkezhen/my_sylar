#include "iomanager.h"
#include "macro.h"
#include "log.h"

#include <sys/epoll.h>
#include <memory>
#include <functional>
#include <errno.h>
#include <fcntl.h>
#include <string>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

//epoll_ctl类型封装成个枚举
enum EpollCtlOp {
    // EPOLL_CTL_ADD,
    // EPOLL_CTL_MOD,
    // EPOLL_CTL_DEL
};

//重载operator<<,用于输出EpollCtlOp枚举类型
static std::ostream& operator<<(std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl)            \
        case ctl :         \
            return os<<#ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default :
            return os<<(int)op;
    }
#undef XX
}

//重载operator<<，用于输出epoll时间标志
static std::ostream& operator<<(std::ostream& os, EPOLL_EVENTS events) {
    if (!events) return os<<"0";
    //用于控制|号的输出
    bool first = false;
#define XX(E)           \
    //检查E中是否包含事件E\
    if (events & E) {   \
        if (!first) {   \
            os << "|";  \
        }               \
        os << #E;       \
        first = false;  \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

//获取事件上下文
IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch(event) {
        case IOManager::READ :
            return read;       //返回read事件的上下文
        case IOManager::WRITE :
            return write;      //返回write事件的上下文
        default:
            SYLAR_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

//重置事件上下文
void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

//触发事件：当fd的某个事件发生时，调用回调函数或调度协程
void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    //确保改事件存在
    SYLAR_ASSERT(events & event);
    //移除该事件
    events = (Event)(events & ~event);
    //获取该事件的上下文
    EventContext& ctx = getContext(event);
    if (ctx.cb) {
        //如果注册了回调函数，则直接调度回调函数
        ctx.scheduler->schedule(&ctx.cb);
    } else {
        //否则，恢复之前被挂起的协程，让他继续执行之前未完成的任务
        //例如：
        //1，某个协程在执行socket读操作，但数据未准备好，被挂起等待EPOLLIN事件
        //2, 现在EPOLLIN事件触发，socket可读
        //3, 通过调度器让协程恢复，继续执行read()读取数据
        ctx.scheduler->schedule(&ctx.fiber);
    }
    //调度完成后，清空事件的调度器指针，表示该事件已经处理完成
    ctx.scheduler = nullptr; 
}

//这里为什么要引入管道：在iomanager中，epoll_wait()负责监听文件描述符的io事件，但如果没有任何事件触发，epoll_wait会一直阻塞
//导致iomanager无法及时响应新的任务。
//此时出现一个问题，如果iomanager需要立即执行一个新的任务，但epoll_wait正在等待io事件而阻塞了怎么办
//解决办法是使用pipe创建一对管道文件描述符m_tickleFds[0]和m_tickleFds[1]
//其中m_tickleFds[0]为读端：交给epoll监听，等待数据可读，EPOLLIN事件
//其中m_tickleFds[1]为写端：当iomanager有新任务时，向m_tickleFds[1]写入一个字节，让m_tickleFds[0]触发EPOLLIN,唤醒epoll_wait从而不阻塞的处理新任务
IOManager::IOManager(size_t threads, bool use_caller, const std::string& name) : Scheduler(threads, use_caller, name) {
    m_epfd = epoll_create(5000);  //最多监听5000个fd
    SYLAR_ASSERT(m_epfd > 0);
    int rt = pipe(m_tickleFds);   //创建管道
    SYLAR_ASSERT(rt > 0);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    //监听RPOLLIN EPOLET(可读，边缘触发)
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];
    //设置管道的读端为非阻塞模式，避免epoll读取数据时被阻塞
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);

    //将pipe读端加入epoll监听，用于通知新任务
    //如果m_tickleFds[1]有数据写入时，m_tickleFds[0]会触发EPOLLIN事件，唤醒epoll_wait，从而执行新的任务
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);

    //初始化FdContext容器
    contextResize(32);    //预分配32个fd上下文

    start();   //启动scheduler
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

//重置m_fdContexts的大小，调整管理的文件描述符的事件上下文的数量
void IOManager::contextResize(size_t size) {
    if (size < m_fdContexts.size()) {
        for (size_t i = size; i < m_fdContexts.size(); ++i) {
            delete m_fdContexts[i];
        }
    }
    m_fdContexts.resize(size);
    for (size_t j = 0; j < size; ++j) {
        if (!m_fdContexts[j]) {
            m_fdContexts[j] = new FdContext;
            m_fdContexts[j]->fd = j;
        }
    }
}

//addEvent负责为fd绑定指定event(读写事件)，并将cb作为回调函数存储，等待epoll触发事件后执行
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }
    //检查fd_ctx是否已经监听该事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(fd_ctx->events & event)) {
        //事件已存在，说明重复添加了
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
            << " event=" << (EPOLL_EVENTS)event
            << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;   //保留原有的事件并添加新的事件
    epevent.data.ptr = fd_ctx;

    //注册或修改fd事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl fail" << (EpollCtlOp)op << (EPOLL_EVENTS)epevent.events;
        return -1;
    }

    //事件数量+1
    ++m_pendingEventCount;
    //记录新事件
    fd_ctx->events = (Event)(fd_ctx->events | event);
    //获取该事件的EventContext，确保调度器、协程、回调函数都未绑定
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);
    
    //绑定
    event_ctx.scheduler = Scheduler::GetThis();  //绑定调度器
    if (cb) {
        event_ctx.cb.swap(cb);   //绑定回调函数
    } else {
        //GetThis返回当前正在执行的协程，即调用addEvent的协程
        //event_ctx.fiber记录当前协程，当fd事件触发时，改协程会被IOManager重写调度执行（调用triggerEvent())
        event_ctx.fiber = Fiber::GetThis();
        //确保协程状态为EXEC（正在执行）
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC, "state= " << event_ctx.fiber->getState());
    }
}

//在epoll事件监听机制中移除某个fd指定的事件，如果该文件描述符fd上不再有其他监听事件，则从epoll监视列表中完全删除该fd
//即如果fd上还有其他事件，则修改epoll监听的事件类型EPOLL_CTL_MOD
//如果fd上没有其他事件了，则直接从epoll中删除这个fd EPOLL_CTL_DEL
//如果fd上没有event这个事件，则直接返回false
bool IOManager::delEvent(int fd, Event event) {
	//检查fd是否有效
	RWMutexType::ReadLock lock(m_mutex);
	if ((int)m_fdContexts.size() <= fd) {
		return false;
	}
	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();

	//检查要删除的事件是否存在
	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
		return false;
	}

	//计算新的事件类型，确定epoll_ctl操作
	Event new_event = static_cast<Event>(fd_ctx->events & (~event));
	int opt = new_event ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
	
	//更新epoll
	epoll_event epevent;
	epevent.events = EPOLLET | new_event;
	epevent.data.ptr = fd_ctx;
	int rt = epoll_ctl(m_epfd, opt, fd, &epevent);

	//错误处理
	if (rt) {
		SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)opt << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	//更新FdContext
	--m_pendingEventCount;
	fd_ctx->events = new_event;
	FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
	//清理event_ctx的回调信息，保证event相关的数据不会被误用
	fd_ctx->resetContext(event_ctx);
	return true;
}

//取消某个fd上指定的event事件，如果该事件存在则触发该事件的回调函数
//如果fd上还有其他事件，则修改epoll监听的事件类型EPOLL_CTL_MOD
//如果fd上没有其他事件了，则直接从epoll中删除这个fd EPOLL_CTL_DEL
//相比于delEvent，该函数额外触发了event关联的回调函数
bool IOManager::cancelEvent(int fd, Event event) {
	//检查fd是否有效
	RWMutexType::ReadLock lock(m_mutex);
	if (fd >= (int)m_fdContexts.size()) {
		return false;
	}
	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();

	//检查事件是否存在，并确定epoll_ctl的opt
	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
		return false;
	}
	Event new_events = (Event)(fd_ctx->events & ~event);
	int opt = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;

	//更新epoll
	epoll_event epevent;
	epevent.events = EPOLLET | new_events;
	epevent.data.ptr = fd_ctx;
	int rt = epoll_ctl(m_epfd, opt, fd, &epevent);
	if (rt) {
		SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)opt << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	//触发event事件(让等待event的协程执行），并减少pendingEventCount
	fd_ctx->triggerEvent(event);
	--m_pendingEventCount;
	return true;
}

//取消fd上的所有事件（READ WRITE)，并立即触发这些事件的回调
bool IOManager::cancelAll(int fd) {
	RWMutexType::ReadLock lock(m_mutex);
	if ((int)m_fdContexts.size() <= fd) {
		return false;
	}
	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();
	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	//检查fd是否有事件
	if (!fd_ctx->events) return false;  //没有事件直接返回不需要取消

	//删除对fd的监听
	int opt = EPOLL_CTL_DEL;
	epoll_event epevent;
	epevent.events = 0;
	epevent.data.ptr = fd_ctx;
	int rt = epoll_ctl(m_epfd, opt, fd, &epevent);
	if (rt) {
		SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)opt << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	//触发READ和WRITE事件，并减少pendingEventCount
	if (fd_ctx->events & READ) {
		fd_ctx->triggerEvent(READ);
		--m_pendingEventCount;
	}
	if (fd_ctx->events & WRITE) {
		fd_ctx->triggerEvent(WRITE);
		--m_pendingEventCount;
	}

	//确保fd_contexts已经被清空，不再有任何事件
	//注意：triggerEvent会清理对应的事件
	//events = (Event)(events & ~event);
	SYLAR_ASSERT(fd_ctx->events == 0);
	return true;
}

//返回当前线程中的IOManager实例
//Scheduler是IOManager的基类，dynamic_cast用于安全进行向下转型，确保Scehduler::GetThis()真的指向一个IOManager实例
IOManager* IOManager::GetThis() {
	//如果转换失败则返回nullptr
	return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

//唤醒IOManager线程，如果有空闲线程，会向m_tickleFds[1]写入一个字符‘T’，触发事件通知
void IOManager::tickle() {
	if (!hasIdleThreads()) {
		return;
	}
	//写入T表示有新的任务或事件，向管道的读端写入，IOManager会监听到，从而被唤醒执行新的任务
	int rt = write(m_tickleFds[1], "T", 1);
	SYLAR_ASSERT(rt == 1);
}

//检查IOManager是否可以停止
bool IOManager::stopping(uint64_t& timeout) {
	//获取距离下一次定时器触发的时间间隔
	timeout = getNextTimer();
	//timeout == ~0ull表示没有定时器任务要执行
	//m_pendingEventCount == 0表示当前没有等待处理的io事件，说明所有事件已经完成
	//stopping()==true表示调度器本身可以停止，即没有任务要执行
	return timeout == ~0ull && m_pendingEventCount == 0
		&& Scheduler::stopping();
}

bool IOManager::stopping() {
	uint64_t timeout = 0;
	return stopping(timeout);
}

//这个函数是IOManager的主循环，在程序运行时，它不断等待io事件、执行定时任务、调度协程，直到IOManager停止运行
//idle负责
//1.监听io事件：调用epoll_wait监听已经注册的io事件，并在事件发生时进行处理
//2.管理超时任务：检查定时任务，如果有过期的定时任务，就执行它们
//3.处理可读可写事件：对于每个触发的epoll_event，查找对应的fdContext，根据事件类型调用相应的回调函数
//4.支持协程调度：在循环尾部，将当前协程swapOut，让出CPU资源，是其他任务有机会执行
void IOManager::idle() {
    // 定义 epoll_wait 一次最多可以返回 256 个事件
    const uint64_t MAX_EVENTS = 256;
    epoll_event* events = new epoll_event[MAX_EVENTS]();
    
    // shared_ptr 除了可以托管对象，也可以托管数组（托管数组实际托管的是数组的首地址），但要提供一个自定义的删除器
    std::shared_ptr<epoll_event[]> shared_events(events, [](epoll_event* ptr) {
        delete[] ptr;
    });

    // 进入事件循环
    while (true) {
        // 检查是否需要停止
        uint64_t next_timeout = 0;
        if (SYLAR_UNLIKELY(stopping(next_timeout))) {
            SYLAR_LOG_INFO(g_logger) << "name=" << getName()
                                     << " idle stopping exit";
            break;
        }

        // 调用 epoll_wait 等待事件
        int rt = 0;
        do {
            // 最大超时时间
            static const int MAX_TIMEOUT = 3000;
            if (next_timeout != ~0ull) {
                next_timeout = (int)next_timeout > MAX_TIMEOUT ? MAX_TIMEOUT : next_timeout;
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            // events: 用于存储返回的事件数组，最多返回 MAX_EVENTS 个事件
            // next_timeout: 确定在没有事件发生时，最多等待多久
            // epoll_wait 返回值是发生事件的文件描述符的数量，即在等待时间内有多少个文件描述符触发了事件
            rt = epoll_wait(m_epfd, events, MAX_EVENTS, (int)next_timeout);
            if (rt < 0 && errno == EINTR) {
                // 出现问题，重新调用 epoll_wait
                continue;
            } else {
                break;
            }
        } while (true);

        // 处理超时任务
        std::vector<std::function<void()>> cbs;
        // 得到所有超时需要执行的任务
        listExpiredCb(cbs);
        if (!cbs.empty()) {
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        // 处理通过 epoll_wait 得到的 events 中的 IO 事件
        for (int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];

            // 处理管道唤醒事件
            // 在 IOManager 这种多线程多协程调度器中，可能会有别的线程需要让 epoll_wait 立即返回
            // 比如有新的任务需要调度、需要修改 epoll 监听的事件、要让 idle 退出等
            // 但是 epoll_wait 默认是阻塞等待，如果没有事件，他会一直等到超时，最多等待 next_timeout 毫秒，而不是立即返回，因此，我们需要一个方法来手动唤醒 epoll_wait，这就是 m_tickleFds 管道的作用
            // 解决办法是使用管道，在写端简单写入数据（tickle 函数中实现），并在该函数 idle 中的 epoll_wait 就会检测到，然后跳出等待，在此处进行读操作，将管道中的数据都读走，使其恢复成不可读状态
            if (event.data.fd == m_tickleFds[0]) {
                uint8_t dummy[256];
                // 读取管道中数据，清空唤醒信号，使管道信号恢复为不可读状态
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0) {
                    continue;
                }
                continue;
            }

            // 处理 FdContext
            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);

            // 处理错误事件
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                // EPOLLERR 表示 fd 发生错误
                // EPOLLHUP 表示 fd 被挂起（如对端关闭）
                // 如果发生这两个错误，尝试触发可读或可写
                // 其实也就是我们得到的事件应该是 EPOLLIN 或 EPOLLOUT 但由于某种异常，我们的 events 被置为异常事件了，所以我们就把 EPOLLIN 和 EPOLLOUT 都试下，真正要执行的事件一定是这两个中的一个
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            // 解析事件类型，real_events 记录真正发生的事件
            int real_events = 0; // NONE 未定义，这里用 0
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            // 更新 epoll 中监听的 fd
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;
            epoll_ctl(m_epfd, op, fd_ctx->fd, &event);

            // 触发事件
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }

            // 让出 CPU 调度其他协程
            // 这里先获得裸指针，再将 sp reset，再用裸指针去切换协程的作用是：
            // 在 swapOut 时，我们会从一个协程切换到另一个协程，如果当前协程没有其他地方持有，则在 reset 后，就会销毁这个协程，如果不 reset 可能造成协程资源泄露、协程切换异常等问题
            Fiber::ptr cur = Fiber::GetThis();
            auto raw_ptr = cur.get();
            cur.reset();
            raw_ptr->swapOut();
            // 注意：.get 函数只是获取到 shared_ptr 内部的裸指针，这个裸指针的生命还是由 shared_ptr 来管理的，不需要咱们自己手动 delete
        }
    }
}


//当有新的定时器任务被插入到定时器队列的最前面时调用，作用是唤醒IOManager线程，让他及时处理新的定时器任务
void IOManager::onTimerInsertedAtFront() {
	tickle();
}

//iomanager的实现原理
//IOManager 继承自 Scheduler 和 TimerManager，不仅具备多线程协程调度能力，
//还能够管理定时器事件。其核心思想是利用 epoll进行高效的 IO 事件监听，并结合协程调度，
//实现异步 IO。同时，TimerManager 提供定时器管理功能，使 IOManager 也能调度定时任务。
//在 IO 事件管理方面，IOManager 维护一个 epoll 实例，并使用 std::vector<FdContext> 存储每个被监听文件描述符的读写事件及其对应的回调协程；
//当 addEvent(fd, event, cb) 被调用时，IOManager 在 epoll 中添加或修改监听事件，并将协程与 FdContext 关联。
//当 epoll_wait 返回时，IOManager 取出就绪的事件，并将对应的协程调度到 Scheduler 任务队列中执行。
//在定时器管理方面，IOManager 继承了 TimerManager，因此可以维护一个最小堆存储定时器，并在 idle 阶段检查最近的定时任务。
//如果有定时任务到期，则取出对应的任务并执行。此外，epoll_wait 的超时时间由定时器的最近超时时间决定，确保定时任务能按时触发。
//为了支持外部唤醒，IOManager 还实现了 wakeup 机制：当有新的 IO 事件或定时任务添加时，会通过 eventfd 或 pipe 唤醒 epoll_wait，避免长时间阻塞，
//从而保证 IO 事件和定时任务都能及时响应。

//iomanager工作流程
//IOManager 在启动时，会创建多个工作线程，每个线程运行 Scheduler 的 run 方法，其中 idle 逻辑是事件循环的核心。首先，epoll_wait 阻塞等待 IO 事件，
//并结合 TimerManager 计算最近的超时任务，动态调整 epoll_wait 的超时时间。当 IO 事件发生或定时器到期时，epoll_wait 返回，IOManager 处理就绪事件：
//对于 IO 事件，从 epoll 结果中取出触发的文件描述符，查找其对应的 FdContext，将关联的协程任务加入调度队列；对于定时任务，从 TimerManager 取出已超时的定时器，
//并执行其回调。若调度器任务队列中有可运行的协程，则将其取出并在工作线程中执行。如果在 idle 阶段发现没有可执行的任务，则继续调用 epoll_wait 进入等待状态，
//同时保证 wakeup 机制可以随时唤醒它。整个流程结合 epoll 提供的高效 IO 事件监听机制，以及 TimerManager 的定时任务调度能力，使 IOManager 既能高效处理高并发的 IO 请求，
//又能精准调度定时任务，确保整个框架既高效又灵活。

}



