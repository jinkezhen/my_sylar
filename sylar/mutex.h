#ifndef __SYLAR_MUTEX_H__
#define __SYLAR_MUTEX_H__

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>

#include "noncopyable.h"

namespace sylar {

/**
 * @brief 信号量
 */
class Semaphore : Noncopyable {
public:
    /**
     * @param[in] count 信号量值的大小
     */
    Semaphore(uint32_t count = 0);
    ~Semaphore();

    /**
     * @brief 获取信号量
     */
    void wait();

    /**
     * @brief 释放信号量
     */
    void notify();

private:
    sem_t m_semaphore;
};

/**
 * @brief 局部锁的模板实现，该结构体用于管理互斥锁(Mutex)，保证在构造时自动加锁，在析构时自动解锁
 */
//适用于独占访问的场景，比如一个线程修改共享数据时，其他线程不能同时访问
template <class T> //T代表锁类型，通常T是一个互斥锁mutex类，比如std::mutex或pthread_mutex_t类
struct ScopedLockImpl {
public:
    ScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }
    ~ScopedLockImpl() {
        unlock();
    }
    void lock() {
        //避免重复加锁，产生死锁
        if (!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }
    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T& m_mutex;
    bool m_locked;
};

/**
 * @brief 局部读锁模板实现，适用于读写锁(RWLock)，用于管理读锁，读锁允许多个线程同时读取，但不能与写锁共存
 */
//std::shared_mutex<std::shared_mutex> lock(relock)；可以认为是一个读锁，对于读锁来说，所有的子线程都可以同时拿到这把锁，不存在某个子线程拿到锁后，其他子线程拿锁阻塞的现象
template <class T>
struct ReadScopedLockImpl {
public:
    ReadScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }
    ~ReadScopedLockImpl() {
        unlock();
    }
    void lock() {
        //避免重复加锁，产生死锁
        if (!m_locked) {
            m_mutex.rdlock();
            m_locked = true;
        }
    }
    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T& m_mutex;
    bool m_locked;
};

/**
 * @brief 局部写锁模板实现，适用于读写锁(RWLock),用于管理写锁，写锁是独占的，即加写锁的线程必须独占资源
*/
//std::unique_mutex<std::shared_mutex> lock(wrlock)；某个子线程拿到该锁后，其他线程想拿读或写锁时都会阻塞，直到当前线程释放掉读锁
template <class T>
struct WriteScopedLockImpl {
public:
    WriteScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.wdlock();
        m_locked = true;
    }
    ~WriteScopedLockImpl() {
        unlock();
    }
    void lock() {
        //避免重复加锁，产生死锁
        if (!m_locked) {
            m_mutex.wdlock();
            m_locked = true;
        }
    }
    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T& m_mutex;
    bool m_locked;
};

/**
 * @brief 普通互斥锁
 */
class Mutex : Noncopyable {
public:
    // 封装普通的pthread_mutex_t，用于实现普通的互斥锁（同一时刻只有一个线程能够持有锁）
    typedef ScopedLockImpl<Mutex> Lock;

    Mutex() {
        pthread_mutex_init(&m_mutex, nullptr);
    }
    ~Mutex() {
        pthread_mutex_destroy(&m_mutex);
    }
    void lock() {
        pthread_mutex_lock(&m_mutex);
    }
    void unlock() {
        pthread_mutex_unlock(&m_mutex);
    }
private:
    pthread_mutex_t m_mutex;
};


/**
 * @brief 空锁，lock/unlock均为空操作，主要用于调试或者某些场景不需要锁但又必须兼容锁接口的情况
 */
class NullMutex : Noncopyable {
    public:
    // 封装普通的pthread_mutex_t，用于实现普通的互斥锁（同一时刻只有一个线程能够持有锁）
    typedef ScopedLockImpl<NullMutex> Lock;

    NullMutex() {
        pthread_mutex_init(&m_mutex, nullptr);
    }
    ~NullMutex() {
        pthread_mutex_destroy(&m_mutex);
    }
    void lock() {
    }
    void unlock() {
    }
private:
    pthread_mutex_t m_mutex;
};

/**
 * @brief 读写锁：封装pthread_rwlock_t，用于实现读写锁
 */
class RWMutex : Noncopyable {
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock;
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    RWMutex() {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    ~RWMutex() {
        pthread_rwlock_destroy(&m_lock);
    }
    void rdlock() {
        pthread_rwlock_rdlock(&m_lock);
    }
    void wrlock() {
        pthread_rwlock_wrlock(&m_lock);
    }
    void unlock() {
        pthread_rwlock_unlock(&m_lock);
    }

private:
    pthread_rwlock_t m_lock;
};

/**
 * @brief 读写锁：封装pthread_rwlock_t，用于实现读写锁
 */
class NullRWMutex : Noncopyable {
public:
    typedef ReadScopedLockImpl<NullRWMutex> ReadLock;
    typedef WriteScopedLockImpl<NullRWMutex> WriteLock;

    NullRWMutex() {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    ~NullRWMutex() {
        pthread_rwlock_destroy(&m_lock);
    }
    void rdlock() {
    }
    void wrlock() {
    }
    void unlock() {
    }

private:
    pthread_rwlock_t m_lock;
};

/**
 * @brief 自旋锁：封装pthread_spinlock_t实现，不会进入阻塞状态，当前线程获取不到锁时，线程会一直等待(自旋)，而不是进入睡眠，适用于锁的持有时间极短的场景，比如高频率的加锁解锁
 */
//普通互斥锁：当一个线程获取不到普通锁时，他会进入阻塞/休眠状态(Blocked)，操作系统会挂起这个线程，让出CPU资源给其他线程运行
//      对于这种情况来说好处是不会浪费CPU资源，因为线程在等待时不会运行，但缺点是线程切换开销大，进入和退出阻塞状态需要内核调度，涉及系统调用(例如：Pthread_mutex_lock可能会导致sleep)
//自旋锁：当一个线程获取不到自旋锁时，他不会进入休眠状态，而是不断尝试获取锁，一致循环执行，知道获取成功
//      对于这种情况来说，优点是避免了线程切换的开销，但浪费了cpu的资源，因为线程不停运行，导致CPU占用率升高，所以自旋锁适用于短时间临界区的加锁
class Spinlock : Noncopyable {
public:
    typedef ScopedLockImpl<Spinlock> Lock;

    Spinlock() {
        pthread_spin_init(&m_mutex, 0);
    }
    ~Spinlock() {
        pthread_spin_destroy(&m_mutex);
    }
    void lock() {
        pthread_spin_lock(&m_mutex);
    }
    void unlock() {
        pthread_spin_unlock(&m_mutex);
    }
private:
    pthread_spinlock_t m_mutex;
};

/**
 * @brief 原子锁：使用std::atomic_flag实现基于CAS(compare and swap)操作的自旋锁
 */
//适用于极短临界区，性能比Spinlock更高
class CASLock : Noncopyable {
public:
    typedef ScopedLockImpl<CASLock> Lock;

    CASLock() {
        //std::atomic_flag默认是未定义状态，clear将他初始化为false，表示锁当前是未被占用的状态
        m_mutex.clear();
    }
    ~CASLock();

    void lock() {
        //如果m_mutex是false(未被锁)，则设置m_mutex为true，并且返回一个false，表示获取锁成功
        //如果m_mutex已经是true,表示锁已经被占用，那么他会一直自旋等待（一直返回true），直到锁被释返回false
        while (std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }
    void unlock() {
        //清除m_mutex将其恢复为false，表示锁已经释放，其他线程可以获取该锁
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
    
private:
    volatile std::atomic_flag m_mutex;
};
}

#endif // !__SYLAR_MUTEX_H__
