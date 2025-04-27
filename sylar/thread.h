/**
 * @file thread.h
 * @brief 线程相关的封装
 * @date 2025-03-12
 * @copyright Copyright (c) 2025年 kezhen.jin All rights reserved
 */

#ifndef __SYLAR_THREAD_H__
#define __SYLAR_THREAD_H__

#include "mutex.h"
#include "noncopyable.h"
#include <functional>
#include <string>
#include <memory>

namespace sylar {

class Thread : Noncopyable {
public:
    typedef std::shared_ptr<Thread> ptr;

    /**
     * @brief 构造函数
     * @param[in] cb 线程执行函数
     * @param[in] name 线程名称
     */ 
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    /**
     * @brief 线程id
     */
    pid_t getId() const {return m_id;}

    /**
     * @brief 线程名称
     */
    const std::string& getName() const {return m_name;}

    /**
     * @brief 等待线程执行完成
     */
    void join();

    /**
     * @brief 获取当前的线程指针
     */
    static Thread* GetThis();

    /**
     * @brief 获取当前线程的名称
     */
    static const std::string& GetName();

    /**
     * @brief 设置当前线程名称
     */
    static void SetName(const std::string& name);

private:
    /**
     * @brief 线程执行函数
     */
    static void* run(void* arg);

private:
    //线程id
    pid_t m_id = -1;
    //线程结构
    pthread_t m_thread = 0;
    //线程执行函数
    std::function<void()> m_cb;
    //线程名称
    std::string m_name;
    //信号量
    Semaphore m_semaphore;
};

}

#endif // !__SYLAR_THREAD_H__
