/**
 * @file singleton.h
 * @brief 单例模式封装
 * @date 2025-02-23
 * @copyright CopyRight (c) 2025 kezhen.jin. All right reserved.
 */

#ifndef __SYLAR_SINGLETON_H__
#define __SYLAR_SINGLETON_H__

#include <memory>
#include <mutex>

namespace sylar {


/*匿名命名空间的作用是限制作用域，使其中的变量、函数或类仅在当前文件内可见，
避免与其他文件中的同名实体发生冲突，相当于 static 关键字在全局作用域的作用
有助于封装实现细节并提高代码安全性。
*/
//采用双重锁检查实现单例模式
namespace {

template<class T, class X, int N>
T& GetInstanceX() {
    static T* single_instance = nullptr;
    static std::mutex mutex;
    if (single_instance == nullptr) {
        std::lock_guard<std::mutex> inner_lock(mutex);
        if (single_instance == nullptr) {
            single_instance = new T();
        }
    }
    return *single_instance;
}

template<class T, class X, int N>
std::shared_ptr<T> GetInstancePtr() {
    static std::shared_ptr<T> single_instance = nullptr;
    static std::mutex mutex;
    if (single_instance == nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        if (single_instance == nullptr) {
            single_instance = std::make_shared<T>();
        }
    }
    return single_instance; 
}
}

/**
 * @brief 单例模式封装类
 * @details T 单例的类型
 *          X 为了创造多个实例对应的tag
 *          N 同一个tag创造多个实例索引
 */

template<class T, class X = void, int N = 0>
class Singleton {
public:
    /**
     * @brief 返回单例裸指针
     */
    static T* GetInstance() {
        static T single_instance;
        return &single_instance;               //使用静态局部变量实现单例模式
        //return GetInstanceX<T, X, N>();      //使用双重锁检查实现单例模式
    }
};

template<class T, class X = void, int N = 0>
class SingletonPtr {
public:
    /**
     * @brief 返回单例智能指针
     */
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> single_instance = std::make_shared<T>();
        return single_instance;
        //return GetInstancePtr<T, X, N>();
    }
};

}

#endif


//为什么静态局部变量可以保证单例？
//静态局部变量在整个程序运行期间只会创建一个实例，且静态局部变量存储在静态存储区，它的生命周期是程序运行的整个期间
//1.因为静态局部变量在第一次调用时创建，并在程序结束时销毁，并且在此过程中只初始化一次
//2.c++11之后static是线程安全的，在多线程环境下只会有一个线程执行static变量的初始化，其他线程会等待初始化完成后直接访问它
