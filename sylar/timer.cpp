#include "timer.h"
#include "util.h"

namespace sylar {

//为TimerManager的timer集合提供一个比较函数。使set按照定时器的触发时间先后排序
bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const {
    //两个指针相同，返回false，（set不允许重复元素）
    if (lhs == rhs) return false;
    //nullptr小于有效指针，nullptr排在前面
    if (!lhs || !rhs) return lhs < rhs;
    //小的排在前面，如果m_next相等，则比较指针地址
    return (lhs->m_next != rhs->m_next) ? (lhs->m_next < rhs->m_next) : (lhs.get() < rhs.get());
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
    : m_recurring(recurring),
      m_cb(cb),
      m_ms(ms),
      m_manager(manager) {
    m_next = sylar::GetCurrentMS() + m_ms;
}

Timer::Timer(uint64_t next) : m_next(next) {
}

bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    m_next = sylar::GetCurrentMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    // 如果新的时间间隔（ms）与当前时间间隔（m_ms）相同，且不需要从当前时间重新计算，直接返回 true
    if (ms == m_ms && !from_now) {
        return true;
    }
    // 获取写锁，确保对共享资源（m_timers）的访问是线程安全的
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    // 检查定时器的回调函数是否存在，如果不存在，说明定时器无效，返回 false
    if (!m_cb) return false;
    auto it = m_manager->m_timers.find(shared_from_this());
    // 如果找不到当前定时器，说明它已经被移除或无效，返回 false
    if (it == m_manager->m_timers.end()) return false;
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if (from_now) {
        // 如果 from_now 为 true，从当前时间开始计算
        start = sylar::GetCurrentMS();
    }
    else {
        // 否则，从原来的起始时间开始计算
        start = m_next - m_ms;
    }
    m_ms = ms;
    // 计算新的下一次触发时间
    m_next = start + m_ms;
    m_manager->addTimer(shared_from_this(), lock);
    // 返回 true，表示
    return true;
}

TimerManager::TimerManager() {
    //m_previouseTime：用于记录定时器管理器上一次检查定时器的时间戳
    //这个成员的作用是检查系统时间是否有回滚
    //如果系统时间被手动调整到了过去的时间（例如，从 2023 年 10 月 1 日调整到 2022 年 10 月 1 日），定时器的逻辑可能会出现问题。
    m_previouseTime = sylar::GetCurrentMS();
}

TimerManager::~TimerManager() {
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
    , bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}


//定时器触发时执行的回调函数，检查条件是否满足并执行真正的回调函数
//而条件是否满足指的是，参数weak_cond是否有效，仍然有效时才会执行真正的回调函数cb
static void OnTime(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    //尝试将弱引用转换为强引用
    std::shared_ptr<void> weak_ptr = weak_cond.lock();
    if (weak_ptr) {
      cb();
    }
}

/**
* @brief 添加一个条件定时器
* @param ms 定时器的间隔时间（毫秒）
* @param cb 定时器触发时需要执行的回调函数（std::function<void()>）
* @param weak_cond 条件的弱引用（std::weak_ptr<void>），用于检查条件是否仍然有效
* @param recurring 是否是一个循环定时器（即是否重复触发）
* @return 返回创建的定时器对象（Timer::ptr）
*/
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) {
    // 使用 std::bind 将 OnTimer 函数与 weak_cond 和 cb 绑定在一起，生成一个新的回调函数
    // 这个新的回调函数会在定时器触发时调用 OnTimer，并检查 weak_cond 是否有效，有效时才会执行cb
    return addTimer(ms, std::bind(&OnTime, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if(m_timers.empty()) {
      return ~0ull;
    }

    const Timer::ptr& next = *m_timers.begin();
    uint64_t now_ms = sylar::GetCurrentMS();
    if(now_ms >= next->m_next) {
      return 0;
    } else {
      return next->m_next - now_ms;
    }
    }

    /**
    * @brief 获取距离下一个定时器触发的时间间隔（毫秒）
    * @return 返回时间间隔（毫秒），具体含义如下：
    *         - 0：表示下一个定时器已经到期，应该立即触发。
    *         - 正整数值：表示距离下一个定时器触发的时间间隔。
    *         - ~0ull（最大值）：表示没有定时器。
    */
    uint64_t TimerManager::getNextTimer() {
    // 获取读锁，确保对共享资源（m_timers）的访问是线程安全的
    RWMutexType::ReadLock lock(m_mutex);

    // 重置 m_tickled 标志位，表示当前没有新的定时器插入到集合头部
    m_tickled = false;

    // 如果定时器集合为空，返回最大值（表示没有定时器）
    if (m_timers.empty()) {
      return ~0ull;
    }

    // 获取定时器集合中的第一个定时器（即最近要触发的定时器）
    const Timer::ptr& next = *m_timers.begin();

    // 获取当前时间（毫秒）
    uint64_t now_ms = sylar::GetCurrentMS();

    // 如果当前时间已经超过了定时器的触发时间，返回 0（表示立即触发）
    if (now_ms >= next->m_next) {
      return 0;
    }
    // 否则，返回距离下一个定时器触发的时间间隔
    else {
      return next->m_next - now_ms;
    }
}

//获取已经到期的定时器，并将他们的回调函数存入cbs，供调用者执行
void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<Timer::ptr> expired;
    //先用读锁检查m_timers是否为空
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty()) return;
    }

    //写锁进行操作
    RWMutexType::WriteLock lock(m_mutex);
    //再次判断非空，防止在获取写锁过程中，另一线程刚好清空了m_timers
    if (m_timers.empty()) return;

    bool rollover = detectClockRollover(now_ms);
    //如果没有发生回拔，并且最早要执行的定时器也在当前时间之后，则直接返回
    if (!rollover && ((*m_timers.begin())->m_next > now_ms)) return;

    //创建一个定时器，用于查找m_timers中所有超时的定时器
    Timer::ptr now_timer(new Timer(now_ms));
    //lower_bound返回m_timers中第一个m_next >= now_ms的定时器，如果发生了回拔表示所有定时器都过期了
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);

    //由于lower_bound返回的是第一个>=now_ms的迭代器，而有可能有很多个=m_ms的，所以需要一个while循环确保it指向第一个未到期的定时器
    while (it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }

    //移除所有已到期的定时器,并移动到expired中
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);

    cbs.resize(expired.size());
    for (auto& timer : expired) {
      cbs.push_back(timer->m_cb);

      //处理周期性定时器
      if (timer->m_recurring) {
        //如果是周期性的定时器，处理完时间后再次入队
        timer->m_next = now_ms + timer->m_ms;
        m_timers.insert(timer);
      } else {
        //直接置空
        timer->m_cb = nullptr;
      }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
  auto it = m_timers.insert(val).first;   //auto实际为pair<iterator, bool>，first表示新插入元素的位置，second表示是否成功插入
  //检查是否是排在最前面的定时器
  bool at_front = (it == m_timers.begin()) && !m_tickled;
  if (at_front) m_tickled = true;
  lock.unlock();
  if (at_front) {
    onTimerInsertedAtFront();
  }
}

//检查系统时钟是否发生了回绕，即时间是否出现异常跳变
bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if (now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000)) {
      rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}