#include <string>

#include "thread.h"
#include "util.h"
#include "log.h"

namespace sylar {

//thread_local关键字：表示每个线程都有自己独立的一份数据
//当前线程对应的Thread对象指针
static thread_local Thread* t_thread = nullptr;
//当前线程的名称
static thread_local std::string t_thread_name = "UNKNOW";

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread* Thread::GetThis() {
    return t_thread;
}

const std::string& Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string& name) {
    if (name.empty()) return;
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

//子线程初始化的流程
// 1.主线程调用Thread的构造函数，在构造函数中pthread_create创建子进程，子进程执行run，主线程调用m_semaphore.wait()进入阻塞状态
// 2.子线程run开始执行，设置t_thread,t_thread_name,初始化m_id,调用thread->m_semaphore.notify()唤醒主线程，然后执行线程任务cb()
// 3.主线程被唤醒，wait()解除阻塞，Thread构造函数完成，Thread对象成功创建
//注意：子线程执行任务cb()和主线程在子线程初始化完成后解除阻塞后执行的某些动作是同时进行的
Thread::Thread(std::function<void()> cb, const std::string& name) : m_cb(cb), m_name(name) {
    if (name.empty()) m_name = "UNKNOW";
    //参数3(start_routine)：线程函数指针，指定线程的入口函数（新创建的线程一启动时，最先执行的函数）
    //参数4(arg)：传递给start_routine的参数，这里传入当前Thread对象指针，让run方法可以访问Thread成员变量
    //pthread_create的第三个参数只接受痛痛的全局函数和静态函数，而非普通的成员函数，在c++中普通的成员函数有一个隐藏的this指针，他只能在对象的上下文中使用
    //所以这里传入的是静态成员函数static，他不能直接访问m_cb等成员变量，所以run需要借助传入的this指针
    //上一行对应的实际就是，如果一个静态成员函数想要访问普通成员变量，那就可以给这个静态成员函数传入一个该类型对象的参数，这样就能在静态成员函数中访问到非静态成员变量
    int ret = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (ret) {
        // SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt = " << ret << "name = " << name;
        throw std::logic_error("pthread_create error");
    }
    //此处调用wait的作用给是让主线程阻塞，直到子线程初始化完成(运行并释放信号量)
    //在该Thread的实现中，信号量是在run中释放的
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        pthread_detach(m_thread);
    }
}

//join通常是由主线程调用，目的是等待某个子线程结束，然后再执行后续逻辑
void Thread::join() {
    if (m_thread) {
        //pthread_join会阻塞，直到让当前线程等待m_thread执行完成任务cb()后才会解除阻塞
        int ret = pthread_join(m_thread, nullptr);
        if (ret) {
            // SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt = " << ret << "name = " << name;
            throw std::logic_error("pthread_join error");
        } else {
            //线程成功收回，设置为0，避免重复调用pthread_join
            m_thread = 0;
        }
    }
}

void* Thread::run(void* arg) {
    //pthread_create会执行run函数
    //pthread_create(&m_thread, nullptr, &Thread::run, this),将这段代码中传入的this强转成Thread
    Thread* thread = (Thread*)arg;
    t_thread = thread;
    t_thread_name = thread->m_name;
    
    thread->m_id = sylar::GetThreadId();

    //设置线程名称，linux限制名称最多15字节
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    //将m_cb的所有权转移到局部变量cb，确保子线程独占m_cb的执行权，这么做的目的是保证m_cb只由子线程持有，避免主线程和子线程的竞争
    //如果在run()中直接使用m_cb那么主线程和子线程都能访问m_cb，存在潜在的线程安全问题，比如主线程中Thread对象提前销毁，子线程仍在使用Thread对象，那么在子线程中访问m_cb就会出现未定义行为
    std::function<void()> cb;
    //等价于：cb = std::move(thread->m_cb); thread->m_cb = nullptr;
    cb.swap(thread->m_cb);

    //通知m_semaphore，让Thread构造函数接触wait阻塞
    thread->m_semaphore.notify();

    //执行线程任务
    cb();

    return 0;
}
}

