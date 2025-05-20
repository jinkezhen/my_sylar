#include "fd_manager.h"
#include "hook.h"
#include <algorithm>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace sylar {

FdCtx::FdCtx(int fd)
	: m_isInit(false)
	, m_isSocket(false)
	, m_sysNonblock(false)
	, m_userNonblock(false)
	, m_isClosed(false)
	, m_fd(fd)
	, m_recvTimeout(-1)
	, m_sendTimeout(-1) {
	init();
}

FdCtx::~FdCtx() {

}

//初始化FdCtx对象的状态，判断它是否是一个socket，并设置非阻塞模式
bool FdCtx::init() {
	//已经初始化，则直接返回
	if (m_isInit) return true;
	//-1表示不设置超时时间
	m_recvTimeout = -1;
	m_sendTimeout = -1;

	//stat结构体,<sys/stat.h>中，是linux系统调用fstat(), stat()使用的结构体，用于存储文件的元数据（如文件类型、大小、权限、时间戳等）
	struct stat fd_stat;
	//fstat获取m_fd的文件状态信息，并存入fd_stat
	if (-1 == fstat(m_fd, &fd_stat)) {  //m_fd无效
		m_isInit = false;
		m_isSocket = false;
	}
	else {
		m_isInit = true;
		m_isSocket = S_ISSOCK(fd_stat.st_mode);
	}

	//如果是socket就查询它当前的文件状态标志
	if (m_isSocket) {
		int flags = fcntl(m_fd, F_GETFL, 0);
		//如果m_fd不是非阻塞的，就设置为非阻塞
		if (!(flags & O_NONBLOCK)) {
			//为什么一定要设置为非阻塞，因为在sylar框架的io事件循环中，socket必须是非阻塞的，否则hook机制无法正常工作
			fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
			m_sysNonblock = true;
		}
		else {
			m_sysNonblock = false;
		}
	}
	//表示用户还没有手动修改O_NONBLOCK状态，初始的O_NONBLOCK是由框架设置，而非用户手动设置
	m_userNonblock = false;
	//当前fd为打开状态, 初始化时一定为未关闭的
	m_isClosed = false;
	return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) {
	if (type == SO_RCVTIMEO) {
		m_recvTimeout = v;
	}
	else if (type == SO_SNDTIMEO) {
		m_sendTimeout = v;
	}
	else {
		return;
	}
}

uint64_t FdCtx::getTimeout(int type) {
	if (type == SO_RCVTIMEO) {
		return m_recvTimeout;
	}
	else if (type == SO_SNDTIMEO) {
		return m_sendTimeout;
	}
	else {
		return 0;
	}
}

FdManager::FdManager() {
	m_datas.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create) {
	if (fd == -1) return nullptr;
	RWMutexType::ReadLock lock(m_mutex);
	if ((int)m_datas.size() <= fd) {
		if (!auto_create) {
			return nullptr;
		}
	}
	else {
		//如果fd存在则直接返回，但是如果fd不存在但又不允许创建，则也直接返回
		if (m_datas[fd] || !auto_create) {
			return m_datas[fd];
		}
	}
	lock.unlock();

	RWMutexType::WriteLock lock2(m_mutex);
	FdCtx::ptr ctx(new FdCtx(fd));
	if (fd >= (int)m_datas.size()) {
		m_datas.resize(std::max((int)(fd * 1.5), (int)m_datas.size()*2));
	}
	m_datas[fd] = ctx;
	return ctx;
}

void FdManager::del(int fd) {
	RWMutexType::WriteLock lock(m_mutex);
	if (fd  < 0 || (int)m_datas.size() <= fd) {
		return;
	}
	m_datas[fd].reset();
}

}