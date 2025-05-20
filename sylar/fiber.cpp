// 在sylar框架中，协程切换依靠的是linux提供的支持上下文切换的库，通过上下文的切换来实现协程的切换
// <ucontext>的三大作用：1.保存当前上下文 2.切换到另一个上下文 3.恢复之前的上下文
// 关键api：
// 1.getcontext(ucontext_t * ctx)
//     获取当前上下文，保存到 ctx 变量中。
// 2.setcontext(const ucontext_t * ctx)
//     切换到 ctx 指向的上下文，不再返回原来的地方（因为执行流跳走了）。
// 3.swapcontext(ucontext_t * old, const ucontext_t * new)
//     先保存 old 的当前上下文，然后 切换到 new 的上下文，切换到new的上下文后会执行通过makecontext绑定到new的入口函数
// 4.makecontext(ucontext_t * ucp, void (*func)(), int argc, …)
//     初始化 ucp 结构体，指定 func 作为协程的入口函数，并设置 ucp 运行时使用的栈空间和参数。


// 在 Sylar 这个协程框架中，协程有两种执行方式：

// 被调度器管理的协程（通常是 swapIn() 进入，swapOut() 退出）
// 直接由 call() 方式启动的协程（这种方式下，当前 Fiber 直接调用 call() 进入执行）
// 对于 第一种情况（由调度器管理的协程）：

// 它执行完 m_cb() 后，会切换回 调度器，让调度器决定执行下一个协程。
// 这类协程的入口函数是 MainFunc()，它的最后调用 swapOut()，返回到调度器。
// 对于 第二种情况（call() 方式启动的协程）：

// 它不是由调度器管理，而是由 另一个协程（可能是主协程） 直接 call() 进入的。
// 这类协程执行完 m_cb() 后，不会回到调度器，而是 返回到调用它的协程，也就是 call() 之前所在的协程。
// 这类协程的入口函数是 CallerMainFunc()，它的最后调用 back()，切换回 call() 它的协程。

#include "fiber.h"
#include "config.h"
#include "macro.h"
#include "log.h"
#include "util.h"
#include "scheduler.h"
#include <atomic>

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static std::atomic<uint64_t> s_fiber_id{ 0 };             //全局递增的id，每个新创建的协程都会有一个唯一的id
static std::atomic<uint64_t> s_fiber_count{ 0 };          //当前存在的fiber总数
static thread_local Fiber* t_fiber = nullptr;             //当前正在运行的协程
static thread_local Fiber::ptr t_thread_fiber = nullptr;  //当前线程的主协程

static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");


//栈分配器
class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }
    static void Dealloc(void* v, size_t size) {
        return free(v);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    //如果t_fiber存在，说明当前线程有正在运行的协程
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

Fiber::Fiber() {
    m_state = EXEC;
    SetThis(this);
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, getcontext);
    }
    ++s_fiber_count;
    SYLAR_LOG_INFO(g_logger) << "Fiber::Fiber main";
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller) :
    m_cb(cb),
    m_id(++s_fiber_id)
{
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);
    //获取当前的上下文，存入m_ctx，以便后续makecontext设置Fiber的入口
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    m_ctx.uc_link = nullptr;
    //设置当前Fiber栈地址
    m_ctx.uc_stack.ss_sp = m_stack;
    //设置当前Fiber的栈大小
    m_ctx.uc_stack.ss_size = m_stacksize;
    
    //use_caller决定协程的切换方式，
    if (!use_caller) {
        //Fiber通过调度器Scheduler调度执行,将Fiber::MainFunc与该协程相绑定，作为该协程的入口函数
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    }
    else {
        //Fiber由当前线程直接调用，无需调度器，将Fiber::CallerMainFunc与该协程相绑定，作为该协程的入口函数
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
    }

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}

//在delete或shared_ptr计数清零时调用
Fiber::~Fiber() {
    --s_fiber_count;

    if (m_stack) {
        SYLAR_ASSERT(m_state == INIT || m_state == EXCEPT || m_state == TERM);
        StackAllocator::Dealloc(m_stack, m_stacksize);
    }
    else {    //m_stack==nullptr说明是主协程
        //主协程的cb和state应该是空的和EXEC
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);


        Fiber* curr = t_fiber;
        //判断当前线程正在运行的协程 t_fiber 是否就是这个正在析构的 Fiber。
        //curr==this表示curr是当前线程中正在运行的fiber
        if (curr == this) {
            //将当前线程中正在运行的协程置空，防止t_fiber变成野指针
            SetThis(nullptr);
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id << " total=" << s_fiber_count;
}

//重置协程的执行函数和协程状态
void Fiber::reset(std::function<void()> cb) {
    SYLAR_ASSERT(t_fiber);
    SYLAR_ASSERT(m_state == EXCEPT || m_state == INIT || m_state == TERM);

    m_cb = cb;

    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    //表示协程执行完毕后不会自动跳转到其他地方，而是手动管理调度
    m_ctx.uc_link = nullptr;
    //设置当前Fiber栈地址
    m_ctx.uc_stack.ss_sp = m_stack;
    //设置当前Fiber的栈大小
    m_ctx.uc_stack.ss_size = m_stacksize;

    //让 m_ctx 执行 Fiber::MainFunc，并在 m_stack 上运行。
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT;
}

//从当前线程的主协程t_thread_fiber切换到this协程，并执行它。适用于主协程直接调用某个子协程（而不是调度器管理的）
void Fiber::call() {
    SetThis(this);
    m_state = EXEC;
    if (swapcontext(&t_thread_fiber->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//从当前协程返回到主协程t_thread_fiber，表示this任务执行完成了，回到主协程
void Fiber::back() {
    SetThis(t_thread_fiber.get());
    if (swapcontext(&m_ctx, &t_thread_fiber->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//从调度器的主协程切换到子协程(this)执行
void Fiber::swapIn() {
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC);
    m_state = EXEC;
    //保存调度器的主协程的上下文，开始执行this协程
    if (swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//当前协程(this)不再继续执行，而是切换回调度器的主协程，即Scheduler::GetMainFiber()，即把执行权交还给调度器
void Fiber::swapOut() {
    SetThis(Scheduler::GetMainFiber());
    if (swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontex");
    }
}

//设置f为当前线程正在运行的协程t_fiber
void Fiber::SetThis(Fiber* f) {
    t_fiber = f;
}

//获取当前正在运行的协程
Fiber::ptr Fiber::GetThis() {
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

//当前协程让出CPU，切换到后台，同时标记为READY(可执行)
//换句话说就是停掉当前正在运行的子协程并设置好状态，然后切换回当前线程的主协程
//但是由于状态被设置为了READY，所以调度器会在合适的时机选择READY状态的协程，重新swapIn这个Fiber，然后继续执行该READY(未被完全执行完的)子协程
void Fiber::YieldToReady() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    //标记为可执行，表示可以被重新调度执行
    cur->m_state = READY;
    cur->swapOut();
}

//停掉当前子协程，但并不改变状态(所有不会被自动调度，需要自己调度)，切换回主协程，
void Fiber::YieldToHold() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    cur->swapOut();
}

//返回当前进程的总协程数
uint64_t Fiber::TotalFibers() {
    return s_fiber_count;
}

//协程的入口函数：当一个协程开始执行时，实际上运行的就是这个函数
void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);

    try {
        cur->m_cb();
        //状态改为已执行完
        cur->m_state = TERM;
        cur->m_cb = nullptr;
    }
    catch (std::exception& e) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << e.what()
            << " fiber_id=" << cur->getId()
            << std::endl
            << sylar::BacktraceToString();
    }
    catch (...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl
            << sylar::BacktraceToString();
    }
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();
    //下面正常来说再上一步swapOut后就不会走到
    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

//执行协程任务，并在执行完后，返回到调用它的协程，比如在协程a中call唤醒了协程b，在协程b执行完自己的回调函数(任务)后，会调用back()回到协程a
void Fiber::CallerMainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    }
    catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << cur->getId()
            << std::endl
            << sylar::BacktraceToString();
    }
    catch (...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl
            << sylar::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();
    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

}