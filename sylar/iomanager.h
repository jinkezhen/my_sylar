/**
* @brief 基于Epoll的协程调度器
* @date 2025-03-20
* @copyright Copyright (c) 2025年 All rights reserved
*/

#ifndef __SYLAR_IOMANAGER_H_
#define __SYLAR_IOMANAGER_H_

#include <functional>
#include <sys/epoll.h>
#include <memory>
#include <vector>
#include <string>

#include "scheduler.h"
#include "fiber.h"
#include "mutex.h"
#include "timer.h"

namespace sylar {

//继承Scheduler负责协程调度，调度协程执行io事件
//继承TimerManager管理定时任务，定时触发回调
class IOManager : public Scheduler , public TimerManager {
public:
	typedef std::shared_ptr<IOManager> ptr;
	typedef RWMutex RWMutexType;

	//io事件
	enum Event {
		//无事件
		NODE = 0x0,
		//读事件（EPOLLIN)
		READ = 0x1,
		//写事件（EPOLLOUT)
		WRITE = 0x4
	};

private:

	//FdContext用于管理和处理某个文件描述符fd上的读写事件，主要用于事件驱动的IO处理，
	//当某个IO事件发生时，可以触发对应的协程或回调来处理该事件
	//核心作用是存储文件描述符的事件状态，并支持调度相关的协程或回调函数
	struct FdContext {
		typedef Mutex MutexType;
        
		//EventContextz负责描述一个具体的事件（如可读，可写）的执行环境
		//事件发生后，可以选择调度一个协程执行，或者直接调用回调函数处理
		struct EventContext {
			Scheduler* scheduler = nullptr;   //事件执行的调度器
			Fiber::ptr fiber;                 //事件对应的协程
			std::function<void()> cb;		  //事件的回调函数
		};

		//获取事件上下文：返回对应事件的上下文
		EventContext& getContext(Event event);
		//重置某个事件的上下文
		void resetContext(EventContext& ctx);
		//手动触发某个事件，并执行回调或恢复协程
		void triggerEvent(Event event);


		//读事件上下文，如EPOLLIN
		EventContext read;
		//写事件上下文，如EPOLLOUT
		EventContext write;
		//该FdContext关联的文件描述符，表示哪个文件/网络连接的事件
		int fd = 0;
		//当前fd正在监听的事件，可能是读或写或都有
		Event events = static_cast<Event>(0);
		//互斥锁
		MutexType mutex;
	};


public:
	/**
	* @param[in] threads 线程数量
	* @param[in] use_caller 是否将调用线程包含进去
	* @param[in] name 调度器的名称
	*/
	IOManager(size_t threads, bool use_caller = true, const std::string& name = "");

	~IOManager();

	//添加事件
	//fd socket句柄	
	//event 事件类型
	//cb 事件回调函数
	//添加成功返回0，失败返回-1
	int addEvent(int fd, Event event, std::function<void()> cb);

	//从指定文件描述符中删除指定的事件
	//仅仅删除，不会触发事件执行
	bool delEvent(int fd, Event event);

	//取消指定文件描述符上的指定事件
	//如果时间存在则会执行事件
	bool cancelEvent(int fd, Event event);

	//取消指定文件描述符中关联的所有事件
	bool cancelAll(int fd);

	static IOManager* GetThis();

protected:
	void tickle() override;
	bool stopping() override;
	void idle() override;
	void onTimerInsertedAtFront() override;

	//重置socket事件上下文的容器m_fdContexts的大小
	void contextResize(size_t size);

	//判断IOManager是否可以停止运行,通常指IOManager的主循环idle是否可以停止
	//timeout是一个输出参数，返回最近一个定时器任务的触发时间间隔。
	bool stopping(uint64_t& timeout);

private:
	//epoll文件句柄：用于管理所有io事件
	int m_epfd = 0;
	
	//pipe 管道文件句柄：用于通知iomanager有新任务到达
	//它是一个包含两个文件描述符的数组，通常由pipe系统调用创建	
	//m_tickleFds[0]：是读端文件描述符，该文件描述符会被添加到epoll实例中，监听读事件
	//m_tickleFds[1]：是写端文件描述符
	int m_tickleFds[2] = { -1, -1 };
	
	//当前等待执行的事件数量
	std::atomic<size_t> m_pendingEventCount = { 0 };

	//socket事件的上下文容器：存储所有文件描符的上下文
	std::vector<FdContext*> m_fdContexts;

	RWMutexType m_mutex;
};

}

#endif