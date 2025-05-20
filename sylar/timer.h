/**
 * @file timer.h
 * @brief 定时器封装
 * @date 2019-05-28
 */

#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <set>
#include "thread.h"
#include "mutex.h"

namespace sylar{

class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
//TimerManager可以访问Timer的私有成员
friend class TimerManager;
public:
    typedef std::shared_ptr<Timer> ptr;

    //取消定时器：从TimerManager中移除定时器，防止其执行
    bool cancel();
    //刷新定时器的执行时间：延后任务的触发时间（用于心跳机制）
    bool refresh();
    //重置定时器的间隔时间
    //ms:修改后的间隔时间
    //from_now:为true时，表示从当前时间开始计算超时时间，为false时以原触发时间为基准
    bool reset(uint64_t ms, bool from_now);

    /**
     * @brief 构造函数
     * @param[in] ms 定时器的执行间隔（ms）
     * @param[in] cb 触发时执行的回调函数
     * @param[in] recurring 是否为循环定时器
     * @param[in] manager 关联的TimerManager，用于管理定时器的生命周期
     */
    Timer(uint64_t ms, std::function<void()>, bool recurring, TimerManager* manaegr);

    /**
     * @biref 仅用于构造临时Timer，用于排序（比较m_next的时间戳)
     */
    Timer(uint64_t next);

private:
    bool m_recurring = false;           //是否是循环定时器，即是否在执行后继续等待ms再次执行
    uint64_t m_ms = 0;                  //定时器周期ms
    uint64_t m_next = 0;                //定时器任务执行的时间戳
    std::function<void()> m_cb;         //定时器要执行的任务回调函数
    TimerManager* m_manager = nullptr;  //所属的定时器管理类


private:
    //定时器比较器
    struct Comparator {
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
};


//定时器管理类：管理所有Timer，提供定时器添加、查询、删除、获取最新的触发时间等功能
class TimerManager {
friend class Timer;
public:
    typedef RWMutex RWMutexType;

    TimerManager();
    virtual ~TimerManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器的执行间隔（ms）
     * @param[in] cb 触发时执行的回调函数
     * @param[in] recurring 是否为循环定时器
     */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

    //在特定条件下添加一个定时器，当weak_cond仍然有效（即weak_ptr仍然可以提升为shared_ptr）时，才会执行回调函数
    //这样可以避免对象销毁后，回调函数仍然被执行，导致访问无效对象的风险
    //weak_ptr：弱指针，用于控制回调函数师傅执行，如果weak_cond指向的对象已经销毁，则回调函数不会执行
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

    //获取最近触发时间：获取距离最近一次定时器触发的剩余时间ms（即从当前时间到下一次定时器触发时间的间隔）
    uint64_t getNextTimer();

    //获取需要执行的定时器的回调函数列表
    void listExpiredCb(std::vector<std::function<void()>>& cbs);

    //是否有定时器
    bool hasTimer();

protected:
    //当有新的定时器插入到定时器的首部（新插入的timer执行时间最早，所以被排在了最前面），执行此函数
    virtual void onTimerInsertedAtFront() = 0;

    //将定时器添加到管理器中
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);

private:
    //检测系统时间是否被调后，防止时间回退导致定时器异常
    bool detectClockRollover(uint64_t now_ms);


private:
    RWMutexType m_mutex;
    //定时器集合
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    //是否触发onTimerInsertAtFront
    bool m_tickled = false;
    //上次执行时间
    uint64_t m_previouseTime = 0;
};




}

#endif