/**
 * @file scheduler.h
 * @brief 协程调度器封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-05-28
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include "fiber.h"
#include "thread.h"

namespace sylar {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否使用当前调用线程，如果为true，调用Scheduler的当前线程也会成为调度线程
     * @param[in] name 协程调度器名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数
     */
    virtual ~Scheduler();

    /**
     * @brief 返回协程调度器名称
     */
    const std::string& getName() const { return m_name;}

    /**
     * @brief 返回当前线程正在运行的的协程调度器
     */
    static Scheduler* GetThis();

    /**
     * @brief 返回当前协程调度器的调度协程
     */
    static Fiber* GetMainFiber();

    /**
     * @brief 启动协程调度器
     */
    void start();

    /**
     * @brief 停止协程调度器
     */
    void stop();

    /**
     * @brief 调度协程
     * @param[in] fc 协程或函数
     * @param[in] thread 协程执行的线程id,-1标识任意线程
     */
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            //把fc添加到m_fibers中，并返回true或false
            //返回true：意味着m_fibers之前是空的，现在有任务了，需要通知调度器
            //返回false：意味着m_fibers之前已经有任务，不需要额外通知
            need_tickle = scheduleNoLock(fc, thread);
        }

        if(need_tickle) {
            //通知调度器有新任务可执行
            tickle();
        }
    }

    /**
     * @brief 批量调度协程
     * @param[in] begin 协程数组的开始
     * @param[in] end 协程数组的结束
     */
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            //遍历begin-end之间的任务，并全部添加到m_fibers中
            while(begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                ++begin;
            }
        }
        if(need_tickle) {
            tickle();
        }
    }

    //切换到某个线程执行
    void switchTo(int thread = -1);
    //打印调度器状态
    std::ostream& dump(std::ostream& os);
protected:
    /**
     * @brief 通知协程调度器有任务了
     */

    virtual void tickle();
    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 协程无任务可调度时执行idle协程
     */
    virtual void idle();

    /**
     * @brief 设置当前的协程调度器
     */
    void SetThis();

    /**
     * @brief 是否有空闲线程
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0;}
private:
    /**
     * @brief 协程调度启动(无锁)
     */
    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        //如果m_fibers之前是空的，需要唤醒调度器
        bool need_tickle = m_fibers.empty();
        FiberAndThread ft(fc, thread);
        if(ft.fiber || ft.cb) {
            m_fibers.push_back(ft);
        }
        return need_tickle;
    }
private:
    /**
     * @brief 协程/函数/线程组
     */
    struct FiberAndThread {
        /// 协程
        Fiber::ptr fiber;
        /// 协程执行函数
        std::function<void()> cb;
        /// 线程id
        int thread;

        /**
         * @brief 构造函数
         * @param[in] f 协程
         * @param[in] thr 线程id
         */
        //sylar::Fiber::ptr fiber = std::make_shared<sylar::Fiber>(test_func);
        //sylar::FiberAndThread fat1(fiber, 2);
        FiberAndThread(Fiber::ptr f, int thr)
            :fiber(f), thread(thr) {
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         */
        //    sylar::Fiber::ptr fiber = std::make_shared<sylar::Fiber>(test_func);
        //sylar::FiberAndThread fat2(&fiber, 3);
        FiberAndThread(Fiber::ptr* f, int thr)
            :thread(thr) {
            //转移所有权，确保fiber独占传入的Fiber::ptr
            fiber.swap(*f);
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程执行函数
         * @param[in] thr 线程id
         */
        FiberAndThread(std::function<void()> f, int thr)
            :cb(f), thread(thr) {
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程执行函数指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         */
        FiberAndThread(std::function<void()>* f, int thr)
            :thread(thr) {
            cb.swap(*f);
        }

        /**
         * @brief 无参构造函数
         */
        FiberAndThread()
            :thread(-1) {
        }

        /**
         * @brief 重置数据
         */
        void reset() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
private:
    /// Mutex
    MutexType m_mutex;
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    /// 待执行的协程队列
    std::list<FiberAndThread> m_fibers;
    /// use_caller为true时有效, 调度协程
    Fiber::ptr m_rootFiber;
    /// 协程调度器名称
    std::string m_name;
protected:
    /// 协程下的线程id数组
    std::vector<int> m_threadIds;
    /// 线程数量
    size_t m_threadCount = 0;
    /// 工作线程数量
    std::atomic<size_t> m_activeThreadCount = {0};
    /// 空闲线程数量
    std::atomic<size_t> m_idleThreadCount = {0};
    /// 是否正在停止
    bool m_stopping = true;
    /// 是否自动停止
    bool m_autoStop = false;
    /// 主线程id(use_caller)
    int m_rootThread = 0;
};


//临时切换当前线程的调度器，并在析构时恢复原来的调度器，它的典型用法是在某个作用域内暂时使用一个新的调度器，当SchedulerSwitcher离开作用域时，会自动恢复到原来的调度器
class SchedulerSwitcher : public Noncopyable {
public:
    //切换当前调度器为target
    SchedulerSwitcher(Scheduler* target = nullptr);
    //恢复原来的调度器
    ~SchedulerSwitcher();
private:
    Scheduler* m_caller;
};

}

#endif