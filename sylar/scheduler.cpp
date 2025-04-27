#include "scheduler.h"
#include "log.h"
#include "macro.h"
// #include "hook.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
//每个线程都有自己独立的Scheduler和Fiber，不能多个线程共用同一个调度器指针，否则会导致数据竞争

//线程局部变量（生命周期是线程级的），代表当前线程绑定的Scheduler
static thread_local Scheduler* t_scheduler = nullptr;
//线程局部变量，表示当前线程的调度协程（即m_rootFiber：调度Fiber任务的主协程）
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name) : m_name(name) {
    //确保threads至少是1，否则无法调度协程
    SYLAR_ASSERT(threads > 0);
    if (use_caller) {
        sylar::Fiber::GetThis();  //确保当前线程的主协程已经创建
        --threads;                //当前线程参与调度，少创建一个新线程
        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;       //将当前Scheduler绑定到t_scheduler
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        sylar::Thread::SetName(m_name);
        t_scheduler_fiber = m_rootFiber.get();
        m_rootThread = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    }
    else {
        m_rootThread = -1;
    }
    m_threadCount = threads;   //记录剩余的线程数量，不包括use_caller线程
}

Scheduler::~Scheduler() {
    SYLAR_ASSERT(m_stopping);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber() {
    return t_scheduler_fiber;
}

void Scheduler::start() {
    MutexType::Lock lock(m_mutex);
    if (!m_stopping) {   //如果调度器已经运行了，则直接返回
        return;
    }
    m_stopping = false;
    SYLAR_ASSERT(m_threads.empty());   //确保启动前没有线程正在运行
    m_threads.resize(m_threadCount);
    for (int i = 0; i < m_threadCount; ++i) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();
}

void Scheduler::stop() {
    m_autoStop = true;
    if (t_scheduler_fiber && m_threadCount == 0 && (m_rootFiber->getState() == Fiber::TERM || m_rootFiber->getState() == Fiber::INIT)) {
        SYLAR_LOG_INFO(g_logger) << this << " stopped";
        m_stopping = true;
        if (stopping()) return;
    }
    if (m_rootThread != -1) {    //如果当前Scheduler是由主线程创建的
        SYLAR_ASSERT(GetThis() == this);
    }
    else {
        SYLAR_ASSERT(GetThis() != this);
    }
    m_stopping = true;
    for (size_t i = 0; i < m_threadCount; ++i) {
        tickle();
    }
    if (m_rootFiber) {
        tickle();
    }
    if (m_rootFiber) {
        if (!stopping()) {
            m_rootFiber->call();
        }
    }
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for (auto& i : thrs) {
        i->join();   //等待所有线程执行完毕
    }
}

void Scheduler::SetThis() {
    t_scheduler = this;
}

void Scheduler::run() {
    SYLAR_LOG_DEBUG(g_logger) << m_name << " run";
    //启用hook，允许协程内使用某些阻塞操作(sleep、socket等)
    // set_hook_enable(true);
    //将当前线程绑定到调度器
    SetThis();

    //判断是否是主线程
    //主线程的初始协程可能是普通函数，而不是fiber，所以无需进行t_scheduler_fiber设置
    //但非主线程的调度需要存储t_scheduler_fiber，避免调度时误操作。在run中协程会切换，所以我们需要保存当前协程的指针，后续可以正确恢复
    if (sylar::GetThreadId() != m_rootThread) {
        t_scheduler_fiber = Fiber::GetThis().get();
    }

    //创建空闲线程，当Scheduler没有任务时，会进入idle_fiber防止线程退出
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    FiberAndThread ft;

     while (true) {
        ft.reset();
        //判断是否需要唤醒其他线程
        bool tickle_me = false;
        //标记是否成功取出任务
        bool is_active = false;

        {
            MutexType::Lock lock(m_mutex);
            auto it = m_fibers.begin();
            while (it != m_fibers.end()) {
                //如果任务被指定到别的线程执行，则跳过
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    ++it;
                    tickle_me = true;
                    continue;
                }

                //确保任务有效
                SYLAR_ASSERT(it->fiber || it->cb);
                //任务如果正在执行，则跳过
                if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue;
                }

                ft = *it;
                m_fibers.erase(it++);
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
            tickle_me |= it != m_fibers.end();
        }

        //tickle_me=true说明有任务是给其他线程执行的，但他们可能还在等待任务，这时调用tickle()唤醒指定的线程
        if (tickle_me) {
            tickle();
        }

        //执行fiber任务
        if (ft.fiber && (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT)) {
            ft.fiber->swapIn();
            --m_activeThreadCount;

            //任务完成后检查状态
            if (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT) {
                //挂起，等待下次执行
                ft.fiber->m_state = Fiber::HOLD;
            } else if (ft.fiber->getState() == Fiber::READY) {
                //说明任务还没执行完，还需要重新调度
                schedule(ft.fiber);
            }
            ft.reset();
        } else if (ft.cb) { //执行普通回调任务
            if (cb_fiber) {
                cb_fiber->reset(ft.cb);
            } else {
                cb_fiber.reset(new Fiber(ft.cb));
            }
            ft.reset();
            cb_fiber->swapIn();
            --m_activeThreadCount;
            if (cb_fiber->getState() ==Fiber::READY) {
                //需要重新调度
                schedule(cb_fiber);
                cb_fiber.reset();
            } else if (cb_fiber->getState() == Fiber::TERM || cb_fiber->getState() == Fiber::EXCEPT) {
                //任务结束，清理资源
                cb_fiber->reset(nullptr);
            } else {
                //其他情况，等待下次执行
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        } else {  //没有任务就进入空闲状态
            if (is_active) {
                --m_activeThreadCount;
                continue;
            }
            if (idle_fiber->getState() == Fiber::TERM) {
                SYLAR_LOG_INFO(g_logger) << "idle fiber term";
                break;
            }

            ++m_idleThreadCount;
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if (idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->m_state = Fiber::HOLD;
            }
        }
     }
}

//唤醒可能处于休眠状态的工作线程，让他们的继续执行调度任务
void Scheduler::tickle() {
    SYLAR_LOG_INFO(g_logger) << "tickle";
}

//检查调度器是否可以停止
bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    //autoStop=true表示调用者要求停止调度器
    //m_stopping：表示调度器进入了停止模式
    //m_fibers.empty() 表示没有要执行的协程任务
    //m_activeThreadCount=0，没有活跃线程正在执行任务
    return m_autoStop && m_stopping && m_fibers.empty() && m_activeThreadCount == 0;
}

//当调度器暂时没有任何任务可执行时，当前线程进入空闲状态，并等待新的任务
void Scheduler::idle() {
    SYLAR_LOG_INFO(g_logger) << "idle";
    
    while (!stopping()) {
        sylar::Fiber::YieldToHold();
    }
}

//将当前执行的协程 调度到指定线程去运行
void Scheduler::switchTo(int thread) {
    //确保当前线程已经附属于某个Scheduler，否则无法进行调度
    SYLAR_ASSERT(Scheduler::GetThis() != nullptr);
    if (Scheduler::GetThis() == this) {
        //thread == -1表示可以在任何线程运行，无需切换
        //thread == sylar::GetThreadId() 当前线程已经是目标线程，也无需切换
        if (thread == -1 || thread == sylar::GetThreadId()) {
            return;
        }
    }
    //将当前协程添加到Scheduler，并指定目标线程thread执行
    //也就是将当前协程添加到了指定线程的调度队列，但不会立即切换到指定线程执行，所以得先hold
    schedule(Fiber::GetThis(), thread);
    Fiber::YieldToHold();  //让出CPU，等待调度器调度自己到thread上执行
}

//输出调度器状态
std::ostream& Scheduler::dump(std::ostream& os) {
    os << "[Scheduler name=" << m_name
        << " size=" << m_threadCount
        << " active_count=" << m_activeThreadCount
        << " idle_count=" << m_idleThreadCount
        << " stopping=" << m_stopping
        << " ]" << std::endl << "    ";
    for (size_t i = 0; i < m_threadIds.size(); ++i) {
        if (i) {
            os << ", ";
        }
        os << m_threadIds[i];
    }
    return os;
}

/**
 * @brief 构造函数：切换到目标调度器，并保存当前调度器上下文（RAII模式）
 * @param target 要切换到的目标调度器指针（允许为空，此时不执行切换）
 */
SchedulerSwitcher::SchedulerSwitcher(Scheduler* target) {
    // 保存当前线程正在使用的调度器（保留原上下文）
    m_caller = Scheduler::GetThis();

    // 如果目标调度器有效，立即切换到目标调度器
    if (target) {
        target->switchTo();  // 执行协程/线程调度器切换操作
    }
}

/**
 * @brief 析构函数：自动切换回构造时保存的原始调度器（RAII模式）
 */
SchedulerSwitcher::~SchedulerSwitcher() {
    // 当对象生命周期结束时，切换回原始调度器
    if (m_caller) {
        m_caller->switchTo();  // 恢复构造时保存的调度器上下文
    }
}



}