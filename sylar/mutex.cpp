#include "mutex.h"
#include <stdexcept>

namespace sylar {

Semaphore::Semaphore(uint32_t count) {
    if (sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init_error");
    }
}

Semaphore::~Semaphore() {
    //sem_destory只能在所有等待线程已退出时调用，否则可能导致未定义行为
    sem_destroy(&m_semaphore);
}

void Semaphore::wait() {
    //等待信号量，如果信号值m_semaphore大于0，则减1并继续执行，如果信号量==0，则阻塞当前线程，直到其他线程notify()增加信号量
    if (sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait_error");
    }
}

void Semaphore::notify() {
    //增加信号量，如果有线程在wait中阻塞，则唤醒其中一个线程继续执行
    //sem_post是原子操作，可以安全的被多个线程调用
    //如果有多个线程在wait，sem_post只会唤醒一个线程(按照FIFO顺序)
    if (sem_post(&m_semaphore)) {
        std::logic_error("sem_post_error");
    }
}



}