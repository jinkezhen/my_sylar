/**
* @file fd_manager.h
* @brief 文件句柄管理类
* @date 2025-03-23
* @copyright Copyright (c) 2025 All rights reserved
*/

#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include <vector>
#include "thread.h"
#include "singleton.h"

namespace sylar {

/**
* @brief 文件句柄上下文类
* @details 管理文件句柄类型(是否是socket)、是否阻塞、是否关闭、是否读写时间超时
*/
class FdCtx : public std::enable_shared_from_this<FdCtx> {
public:
	typedef std::shared_ptr<FdCtx> ptr;
	//通过文件句柄构建FdCtx
	FdCtx(int fd);
	~FdCtx();

	//是否初始化完成
	bool isInit() const { return m_isInit; }
	//fd是否是socket
	bool isSocket() const { return m_isSocket; }
	//fd是否已经关闭
	bool isClose()const { return m_isClosed; }
	
	//用于记录 用户是否主动设置 fd 为非阻塞。
	//如果用户手动设置了 fd 为非阻塞（setUserNonblock(true)），
	//框架在 Hook 相关系统调用时不会再修改它的阻塞状态，
	//完全交由用户控制。
	void setUserNonblock(bool v) { m_userNonblock = v; }
	//获取用户设置的阻塞状态
	bool getUserNonblock()const { return m_userNonblock; }

	//用于记录 框架是否为了协程调度修改了 fd 为非阻塞。
	// 如果 fd 原本是阻塞的，而框架在 Hook 住的系统调用里需要它是非阻塞的，
	// 就会修改 fd，并在操作完成后可能恢复原状（前提是用户没有主动设非阻塞）
	void setSysNonblock(bool v) { m_sysNonblock = v; }
	//获取系统的阻塞状态
	bool getSysNonblock() const { return m_sysNonblock; }

	//设置读写超时时间
	//type SO_RCVTIMEO(读超时)， SO_SNDTIME0(写超时)
	//v 超时时间ms
	void setTimeout(int type, uint64_t v);
	uint64_t getTimeout(int type);

private:
	bool init();

private:
	//注意：这里使用了位域来存储这几个bool类型的变量，:1表示改变量只占用1bit，这些变量是可以共用1字节的，即如果有8个:1的变量，他们有可能都存在于一个字节中
	//是否初始化
	bool m_isInit : 1;
	//改fd是否是socket
	bool m_isSocket : 1;
	//是否是hook设置的非阻塞
	bool m_sysNonblock : 1;
	//是否是用户主动设置的非阻塞
	bool m_userNonblock : 1;
	//改fd是否关闭
	bool m_isClosed : 1;
	//文件句柄
	int m_fd;
	//读超时ms
	uint64_t m_recvTimeout;
	//写超时ms
	uint64_t m_sendTimeout;
};

/**
* @brief 文件句柄管理类
*/
class FdManager {

public:
	typedef RWMutex RWMutexType;
	FdManager();

	/**
	* @brief 获取/创建文件句柄类FdCtx
	* @param[in] fd 文件句柄
	* @param[in] auto_create 如果不存在改句柄对应的FdCtx，是否自动创建该FdCtx
	* @return 返回对应的文件句柄FdCtx::ptr
	*/
	FdCtx::ptr get(int fd, bool auto_create = false);

	//删除指定文件句柄类
	void del(int fd);

private:
	RWMutexType m_mutex;
	//文件句柄集合
	std::vector<FdCtx::ptr> m_datas;
};

//文件句柄管理类单例
typedef Singleton<FdManager> FdMgr;

}

#endif
