#include "hook.h"
#include <dlfcn.h>
#include <cstdarg>
#include <sys/socket.h>

#include "config.h"
#include "log.h"
#include "fiber.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "macro.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
namespace sylar {

//全局配置项
//在配置系统中查找名为tcp.connect.timeout的变量，如果找不到，则创建一个默认值5000，并覆带tcp.connect.timeout的描述信息
static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout = sylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

//线程局部变量：每个线程都会有一个独立的t_hook_enable，他们之间不会相互影响
//用于标记当前线程是否启用hook机制
static thread_local bool t_hook_enable = false;

//宏定义：HOOK_FUN，定义了一组要被Hook(钩子替换)的函数
//XX为占位符，在后续代码中，它会被替换成具体的操作
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

//hook初始化
void hook_init() {
    static bool is_inited = false;
    if (is_inited) return;
//替换系统函数
//##是拼接字符
//dlsym是动态库符号查找函数，用于获取原始的sleep函数地址
//其中RTLD_NEXT作用是查找下一个符合条件的符号(即查找原始的sleep)，而不是当前的hook版本。
//为什么要用RTLD_NEXT ?
//  因为在LD_PRELOAD或动态库hook机制中，我们会用自己的Hook版本函数替换系统原生函数，不能用dlsym(RTLD_DEFAULT, "sleep")因为RTLD_DEFAULT会返回当前可用的sleep，而此时我们hook过的sleep也是当前可用的，可能会导致还是调用到自己，因此使用RTLD_NEXT。
//其中(name##_fun)是强转，将dlsym返回的指针能正确转换成对用的函数指针类型
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
    is_inited = true;
}

//全局TCP连接超时变量
//-1表示没有超时时间，可以无限超时
static uint64_t s_connect_timeout = -1;

//_HookIniter结构体负责初始化hook机制，并设置s_connect_timeout
struct _HookIniter {
    _HookIniter() {
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();
        //添加监听器，当g_tcp_connect_timeout发生变化时，自动更新s_connect_timeout并记录日志
        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value) {
            SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from "
                << old_value << " to " << new_value;
            s_connect_timeout = new_value;
            });
    }
};

//_HookIniter的全局静态实例,确保调用到_HookIniter的构造函数
static _HookIniter s_hook_initer;

//hook开关控制
bool is_hook_enable() {
    return t_hook_enable;
}
void set_hook_enable(bool flag) {
    t_hook_enable = flag;
}

//timer_info结构体用于记录定时器是否被取消
//canceled=0默认定时器未取消
//该结构体的作用是用于异步IO事件的超时管理。在hook机制中，定时器用于管理IO超时，如果某个操作超时或被取消，需要标记cancelled变量
struct timer_info {
    int cancelled = 0;
};


//do_io 函数是 Sylar 框架中的一个 Hook 机制核心函数，它用于拦截常见的 IO 操作（如 read、write 等），
//并结合协程调度器（IOManager）实现非阻塞 IO，同时提供超时控制。其主要逻辑是：如果 Hook 机制未启用，
//直接执行原始 IO 函数；否则，获取文件描述符的上下文信息，检查是否是 socket（ctx->isSocket()）、
//是否已关闭（ctx->isClose()）、是否已设为非阻塞（ctx->getUserNonblock()）等条件，并尝试直接执行 IO 操作。
//如果 fd 不是 socket，则直接调用原始函数，不做 Hook 处理；如果 fd 是 socket 且已关闭，则返回错误 EBADF；
//如果 fd 是 socket 但用户已手动设置为非阻塞模式，则不进行 Hook，直接调用原始 IO 函数。
//如果 IO 操作因 EINTR（信号中断）失败，则自动重试；如果因 EAGAIN（无数据可读 / 写）失败，则注册 IO 事件，
//并挂起当前协程，等待 IO 事件触发或超时后恢复执行，最终确保 IO 操作高效进行，同时避免阻塞线程。在等待过程中，
//如果设置了超时（to != -1），则会创建一个定时器，若超时仍未完成 IO 操作，则取消 IO 事件并返回超时错误 ETIMEDOUT。

// 这段代码的核心目的是实现基于协程的异步 I/O，即在 I/O 无法立即完成时，挂起当前协程，
// 等待 I/O 事件触发后再恢复执行。它的逻辑是对 Linux 传统阻塞式 I/O 进行hook（钩子），
// 将本来可能会阻塞的 I/O 操作变成非阻塞 + 挂起协程的方式，从而提升并发能力。

//fd：文件描述符，要进行IO操作的对象
//OriginFun fun：传入的原始IO函数，如read、write等，用于在Hook机制失效时直接执行原始操作
//const char* hook_fun_name：用于日志记录的IO函数名称（如read、write等）
//event：IO事件类型，READ或WRITE，用于区分读写事件
//timeout_so：具体的超时类型，SO_RCVTIMEO接收超时，SO_SNDTIMEO发送超时
//Args&&....args：Args是一个类型参数包，它可以匹配任意数量的类型；Args&&表示完美转发，可以匹配左值引用、右值引用、常量等各种参数类型；args是变长参数，表示传递给原始IO函数的参数；使用std::forward保持完美转发特性
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name, uint32_t event, int timeout_so, Args&&... args) {
    //判断Hook是否启动
    if (!sylar::t_hook_enable) {
        //如果没有启动hook直接调用原始函数如read、write
        //...是参数包展开运算符，用于将args参数包展开，并传递给fun函数
        return fun(fd, std::forward<Args>(args)...);
    }

    //获取文件描述符的上下文信息
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    //如果fd在管理器中找不到，则说明他是一个普通文件，而非socket，直接调用原始函数
    if (!ctx) {
        return fun(fd, std::forward<Args>(args)...);
    }
    
    //检查文件描述是否关闭
    if (ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    //检查是否是socket，或者用户是否是手动设置为非阻塞,如果手动设置为非阻塞模式，说明这个socket本来就是非阻塞的，则不需要hook机制来模拟非阻塞操作，直接调用原始函数
    if (!ctx->isSocket() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }

    //获取超时时间
    uint64_t to = ctx->getTimeout(timeout_so);
    //tinfo用于超时控制，记录定时器的状态
    std::shared_ptr<timer_info> tinfo(new timer_info);

//retry是一个代码标记点，表示从这里开始执行IO操作
//配合goto可以在出现问题时跳转回这里重新执行
retry:
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    //当read或write被信号(如SIGINT)打断时会返回-1，并设置errno==EINTR，这时io没有真正出错，只需要重新调用io函数即可
    while (n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }

    //处理EAGAIN(资源暂时不可用，需要等待)
    //对于非阻塞的socket出现EAGAIN的原因
    //1.对于read():当前没有数据可读
    //2.对于write()：缓冲区已经满了，无法写入数据
    if (n == -1 && errno == EAGAIN) {
        //这里不能直接返回错误，而是注册事件，等待io事件触发
        //即如果IO不能立即执行就挂起协程，等待IO事件触发后再继续
        //获取当前线程的io管理器实例
        sylar::IOManager* iom = sylar::IOManager::GetThis();

        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        //如果设置了超时，则创建定时器
        if (to != (uint64_t)-1) {
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                    auto t = winfo.lock();
                    //!t表示tinfo已经被销毁，cancelled=true表示io已完成直接返回，不执行超时逻辑
                    if (!t || t->cancelled) {
                        return;
                    }
                    //标记IO操作因为超时而被取消
                    t->cancelled = ETIMEDOUT;
                    iom->cancelEvent(fd, (sylar::IOManager::Event)event);
                }, winfo);
        }

        //注册fd的IO事件，监听event
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));
        if (SYLAR_UNLIKELY(rt)) {
            SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
            << fd << ", " << event << ")";
            if (timer) {
                timer->cancel();
            }
            return -1;
        } else {
            //挂起当前协程，切换到其他任务;等待IO事件触发,当 I/O 事件发生后，协程会被重新唤醒
            sylar::Fiber::YieldToHold();
            if (timer) {
                timer->cancel();
            }
            if (tinfo->cancelled) {
                errno = tinfo->cancelled;
                return -1; // 如果 tinfo->cancelled 不为 0，说明 I/O 发生超时，返回错误
            }
            goto retry;
        }
    }
    return n;
}

extern "C" {

#define XX(name) name##_fun name##_f = nullptr;
#undef XX

//sleep hook版本
unsigned int sleep(unsigned int seconds) {
    //如果hook机制未启用，直接调用原始sleep函数，sleep_f指向了原始sleep函数
    if (!sylar::t_hook_enable) {
        return sleep_f(seconds);
    }

    //这个sleep是协程级别的，他不会真让线程睡眠，而是把当前协程挂起，稍后再恢复
    //需要保存当前的协程指针，以便稍后重新调度该协程
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    //iomanager用于定时调度当前协程
    sylar::IOManager* iom = sylar::IOManager::GetThis();

    //创建一个定时器，当seconds秒后执行一个回调函数，这里的回调函数就是scheduler(fiber，-1)
    //scheduler(fiber，-1)作用是在某个线程上(-1：任意一个合适的线程)上重新调度当前协程fiber
    //这样sleep不会让整个线程阻塞，而只是挂起当前协程，等seconds秒后再恢复执行当前协程
    //传统的sleep会让整个线程休眠，导致该线程上的所有协程无法执行，这里的sleep只是让当前协程暂停，但当前线程的其他协程仍可以继续执行，提高了并发能力
    iom->addTimer(seconds * 1000, 
        //(void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))：作用是告诉编译器这个指针（&sylar::IOManager::schedule）指向Scheduler的成员函数；这个函数的参数是（sylar::Fiber::ptr，int thread）；这个函数的返回值是void
        //std::bind的绑定结果就是执行iom->scheduler(fiber, -1);这里的std::bind绑定了一个类的成员函数，所以第二个参数需要传入一个对应的类实例，如果是静态成员函数则不需要传入类实例，只需要向绑定普通函数一样传入需要的参数即可；
        std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
                    , iom, fiber, -1));  //iom：调用schedule的实例   fiber、-1是传给schedule的参数

    //主动让出CPU资源，并挂起当前协程，等待定时器触发后，重新调度这个协程，使其继续运行
    sylar::Fiber::YieldToHold();
}



int usleep(useconds_t usec) {
    if (!sylar::t_hook_enable) {
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();

    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)
        (sylar::Fiber::ptr, int thread)) & sylar::IOManager::schedule
        , iom, fiber, -1));
    sylar::Fiber::YieldToHold();
    return 0;
}

//原始的nanosleep作用：让当前线程进入休眠最少休眠时间是ns，第一个参数struct timespec* req代表希望休眠的时间；第二个参数struct timespec* rem代表如果nanosleep被信号中断，rem记录剩余时间（可用于继续休眠）
int nanosleep(const struct timespec* req, struct timespec* rem) {
    //req希望休眠的时间
    //rem如果nanosleep被中断，该参数会存储剩余的时间
    if (!sylar::t_hook_enable) {
        return nanosleep_f(req, rem);
    }
    //转为ms
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(timeout_ms, std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread)) & sylar::IOManager::schedule, iom, fiber, -1));
    sylar::Fiber::YieldToHold();
    return 0;
}

//应用程序收到的fd和原始socket调用一致，但sylar额外注册了fd，方便管理
//domain：协议族AF_INET  AF_INET6
//type：套接字类型 SOCK_STREAM tcp   SOCK_DGRAM udp
//protocol：通常设置为0，让系统自动选择默认协议
int socket(int domain, int type, int protocol) {
    if (!sylar::t_hook_enable) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if (fd == -1) return fd;  //出现错误直接返回
    //查找或查找fd对应的管理对象
    sylar::FdMgr::GetInstance()->get(fd, true);
    return fd;
}

//该函数是sylar框架hook版本的connect，用于非阻塞connect并支持超时控制
//原始的connect调用：如果socket是阻塞模式，connect可能会长时间阻塞(直到连接建立或失败)
//fd：socket文件描述符
//addr：目标地址
//addrlen：地址结构的大小
//timeout_ms：超时时间ms，-1代表不设置超时
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if (!sylar::t_hook_enable) {
        return connect_f(fd, addr, addrlen);
    }
    //获取fd对应的FdCtx
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if (!ctx || ctx->isClose()) {  //ctx无效或关闭
        errno = EBADF;
        return -1;
    }
    //如果fd不是socket调用原始socket
    if (!ctx->isSocket()) {
        return connect_f(fd, addr, addrlen);
    }
    //如果socket已经设置为用户非阻塞模式，不需要hook
    if (ctx->getUserNonblock()) {
        return connect_f(fd, addr, addrlen);
    }

    //调用原始connect
    int n = connect_f(fd, addr, addrlen);
    if (n == 0) {  //connect立即成功则返回0
        return 0;
    }
    else if (n != -1 || errno != EINPROGRESS) { //发生了其他错误
        return n;
    }

    sylar::IOManager* iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);
    if (timeout_ms != (uint64_t)-1) {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
            auto t = winfo.lock();
            if (!t || t->cancelled) return;
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, sylar::IOManager::WRITE);
        }, winfo);
    }
    //注册WRITE事件：如果fd可写，说明connect已经完成（无论connect成功还是失败）
    int rt = iom->addEvent(fd, sylar::IOManager::WRITE, nullptr);
    if (rt == 0) {
        //让出CPU等待WRITE事件（connect连接完成或连接超时）
        //如果addEvent成功，当前协程进入HOLD状态，让出CPU，直到WRITE事件触发时，协程才会被resume唤醒
        sylar::Fiber::YieldToHold();
        //再次执行到这里说明，connect完成(连接成功或超时)
        //取消timer，不再需要
        if (timer) {
            timer->cancel();
        }
        //检查是否是超时导致的WRTIE触发
        if (tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    }
    else {   //addEvent失败
        if (timer) timer->cancel();
        SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    //检查connect的错误状态
    int error = 0;
    socklen_t len = sizeof(int);
    if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) return -1;
    if (error == 0) { //连接成功
        return 0;
    }
    else {
        errno = error;
        return -1;
    }
}

int connect(int fd, const struct sockaddr* addr, socklen_t addrlen) {
    return connect_with_timeout(fd, addr, addrlen, sylar::s_connect_timeout);
}

//accept是服务端用于接收新连接的系统调用
//sockfd：服务端监听的套接字，即listen的fd
//addr：存放新连接客户端的地址信息（如IP、端口）
//addrlen：addr结构体的大小，调用结束后会被更新为实际大小
//accept成功返回一个新的fd，代表已经建立新连接，失败返回-1
int accept(int sockfd, const sockaddr* addr, socklen_t* addrlen) {
    //当有新的客户端连接到来时，accept对应的fd变为可读，所以需要监听READ
    //如果accept直接成功，返回新连接的fd，如果需要等待，会监听READ事件，在可读时恢复协程
    int fd = do_io(sockfd, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    //注册accept返回的新连接fd到FdMgr中
    if (fd >= 0) {
        sylar::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

//从文件描述符fd中读取最多count字节的数据到buf中
ssize_t read(int fd, void* buf, size_t count) {
    return do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);
}

//一次性从文件描述符fd读取数据，并分散到iov指定的多个缓冲区中
//iov：iovec结构体数组，表示多个缓冲区。
    //struct iovec {
    //    void* iov_base; //指向缓冲区的指针；
    //    size_t iov_len; //缓冲区的大小；
    //}
    //strut iovec iov[2];
    //char buf1[100], buf2[100];
    //iov[0].iov_base = buf1;
    //iov[0].iov_len = 100;
    //iov[1].iov_base = buf2;
    //iov[1].iov_len = 100;
//iovcnt：iov数组中元素个数，即有多少个缓冲区
ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

//从socket读取数据，存入buf，适用于网络通信
//buf：用于存储接收到的数据的缓冲区
//len：buf的大小，指定最大可接收的字节数
//flags：控制行为的标志位，一般为0
ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

//从socket读取数据到buf中，并获取发送方的地址信息，主要用于UDP
//src_addr：指向sockaddr结构体的指针，存储发送方的地址信息
//addrlen：src_addr结构体的大小
ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

//从fd中接收带控制信息（如获取发送者地址，文件描述符等）的消息
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

//将buf中的count字节数据写入fd
ssize_t write(int fd, const void* buf, size_t count) {
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

//一次性将多个缓冲区的数据写入fd
ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

//向fd发送msg，适用于网络通信
//fd：已连接的套接字描述符
//len：发送数据的字节数
ssize_t send(int fd, const void* msg, size_t len, int flags) {
    return do_io(fd, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

//向to地址发送msg数据，适用于UDP，该方法是一种无连接的数据发送方法
//to：目标地址结构体指针
// tolen：目标地址结构体长度
ssize_t sendto(int fd, const void* msg, size_t len, int flags, const struct sockaddr* to, socklen_t tolen) {
    return do_io(fd, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}


//sendmsg() 是一个更高级的 socket 发送函数，相比 send() 和 sendto()，它提供了更灵活的数据发送方式，支持：
// ✅ 分散-聚集 I/O（Scatter-Gather I/O）：可以一次性发送多个不连续的数据块（struct iovec）。
// ✅ 附带控制信息（Ancillary Data）：可以发送额外的控制数据（如文件描述符、凭据等）。
// ✅ 适用于 TCP 和 UDP：可以用于面向连接（TCP）或无连接（UDP）的通信。
//fd：要发送数据的 socket 文件描述符
//msg：这是 sendmsg() 的核心参数，它是一个结构体，包含所有发送数据和控制信息：
    // struct msghdr {
    //     void         *msg_name;       // 目标地址（UDP 使用，TCP 可设为 NULL）
    //     socklen_t     msg_namelen;    // 目标地址长度
    //     struct iovec *msg_iov;        // 分散-聚集 I/O 数据块数组
    //     size_t        msg_iovlen;     // iovec 数组的长度
    //     void         *msg_control;    // 控制信息（ancillary data）
    //     size_t        msg_controllen; // 控制信息长度
    //     int           msg_flags;      // 接收时的标志（通常不用，设为 0）
    // };
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    return do_io(fd, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}


int close(int fd) {
    if (!t_hook_enable) {
        return close_f(fd);
    }
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if (ctx) {
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        if (iom) {
            iom->cancelAll(fd);
        }
        sylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

//ioctl：是一个通用的设备控制接口，用于控制IO设备的行为
//request用于指定不同的操作
//示例：int flags = 1;  ioctl(sockfd, FIONBIO, &flags);设置为非阻塞模式
int ioctl(int fd, unsigned long int request, ...) {
    //va_list可变参数列表变量
    //va_start(va, last_fixed_arg)：初始化可变参数列表，让其指向第一个可变参数，last_fixed_arg代表最后一个固定的参数
    //va_arg(va, type)：获取下一个参数，并将指针移动到洗一个可变参数位置
    //va_end(va)：结束可变参数处理，清理va_list资源
    va_list va; //实际是一个指针类型，va用于存储可变参数列表的起始位置
    va_start(va, request); //让va指向最后一个固定参数request的后一个参数，即第一个可变参数
    void* arg = va_arg(va, void*);  //取出va当前指向的参数（并强转为void*类型），并将va向后移动一步指向下一个可变参数。在ioctl中，这个arg通常是指向int的指针，用于存储FIONBIO设置的值
    va_end(va); //结束可变参数处理
    //判断是否是 设置/获取非阻塞模式 的请求
    if (request == FIONBIO) {
        //获取非阻塞状态
        bool user_nonblock = !!*(int*)arg;
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
        //如果ctx不存在、fd已关闭、fd不是socket直接调用原始函数
        if (!ctx || ctx->isClose() || !ctx->isSocket()) {
            return ioctl_f(fd, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(fd, request, arg);
}

//获取指定套接字sockfd 某个选项的值
//level：SOL_SOCKET(通用选项) IPPROTO_TCP(tcp选项) IPPROTO_IP(ip选项)
//optname：要获取的选项名称（如SO_RCVTIMEO代表超时时间）
//optval：用于存储获取的选项值
//optlen：存储optval的大小
//示例：获取套接字的超时时间  struct timeval timeout; socklen_t len = sizeof(timeout); getsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, &len);
int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    return getsockopt(sockfd, level, optname, optval, optlen);
}

//设置指定套接字sockfd 某个选项的值
//optval：指向选项值的指针
//optlen：选项值的大小
//示例：设置套接字的超时时间 struct timeval timeout; timeout.tv_sec = 5; timeout.tc_usec = 0; setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))
//此时设置完超时时间后，如果recv在5s内没有接收到数据
int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    if (sylar::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    //level==SOL_SOCKET代表我们正在设置socket层级的选项，而不是传输层或者应用层的选项
    if (level == SOL_SOCKET) {
        //如果在设置读写超时时间
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if (ctx) {
                //如果ctx已经存在，说明这个socket已经被sylar管理，接下来可以修改它的状态
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt(sockfd, level, optname, optval, optlen);
}

// fcntl：文件描述符控制函数，用于修改或获取文件描述符的状态
int fcntl(int fd, int cmd, ...) {
    va_list va;
    va_start(va, cmd);

    switch (cmd) {
        case F_SETFL: {
            // 读取第一个参数
            int arg = va_arg(va, int);
            va_end(va);

            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
            if (!ctx || ctx->isClose() || !ctx->isSocket()) {
                return fcntl_f(fd, cmd, arg);
            }

            // 存储用户的O_NONBLOCK状态
            ctx->setUserNonblock(arg & O_NONBLOCK);

            // 如果系统本身就是非阻塞的则强制非阻塞，否则清除非阻塞
            if (ctx->getSysNonblock()) {
                arg |= O_NONBLOCK;
            } else {
                arg &= ~O_NONBLOCK;
            }
            return fcntl_f(fd, cmd, arg);
        }

        case F_GETFL: {
            va_end(va);
            int arg = fcntl_f(fd, cmd);

            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
            if (!ctx || ctx->isClose() || !ctx->isSocket()) {
                return arg;
            }

            // 如果用户设置了O_NONBLOCK，则返回带有该标志，否则清除该标志
            if (ctx->getUserNonblock()) {
                return arg | O_NONBLOCK;
            } else {
                return arg & ~O_NONBLOCK;
            }
        }

        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
        {
            // 读取第一个参数
            int arg = va_arg(va, int);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }

        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
        {
            va_end(va);
            return fcntl_f(fd, cmd);
        }

        case F_SETLK:
        case F_SETLKW:
        case F_GETLK: {
            // 读取 struct flock* 参数
            struct flock* arg = va_arg(va, struct flock*);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }

        case F_GETOWN_EX:
        case F_SETOWN_EX: {
            // 读取 struct f_owner_exlock* 参数
            struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }

        default:
            va_end(va);
            return fcntl_f(fd, cmd);
            
    }
}

}   //extren "C"

}   //namespace end