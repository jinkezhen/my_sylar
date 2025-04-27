#ifndef __SYLAR_SOCK_STREAM_H__
#define __SYLAR_SOCK_STREAM_H__

#include <memory.h>
#include "stream.h"
#include "socket.h"

namespace sylar {

class SocketStream : public Stream {
public:
    std::shared_ptr<SocketStream> ptr;
    SocketStream(Socket::ptr socket, bool owner = true);
    ~SocketStream();
    virtual int read(void* buffer, size_t length) override;
    virtual int read(ByteArray::ptr ba, size_t length) override;
    virtual int write(const void* buffer, size_t length) override;
    virtual int write(ByteArray::ptr ba, size_t length) override;
    virtual void close() override;

    Socket::ptr getSocket() const { return m_socket; }
    bool isConnected() const;

public:
    Socket::ptr m_socket;
    //该socket是否由这个流对象全权接管，如果全权接管的话，需要在流对象析构时释放掉socket对象
    bool m_owner;
};


}

#endif