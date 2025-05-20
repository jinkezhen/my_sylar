#include "socket_stream.h"

namespace sylar {

SocketStream::SocketStream(Socket::ptr socket, bool owner = true) 
    : m_socket(socket),
      m_owner(owner){
}
SocketStream::~SocketStream() {
    if (m_owner && m_socket) {
        m_socket->close();
    }
}

bool SocketStream::isConnected() const {
    return m_socket && m_socket->isConnected();
}

int SocketStream::read(void* buffer, size_t length) {
    if (!isConnected()) {
        return -1;
    }
    return m_socket->recv(buffer, length);
}

int SocketStream::read(ByteArray::ptr ba, size_t length) {
    if (isConnected()) {
        return -1;
    }
    std::vector<iovec> iov;
    ba->getWriteBuffers(iov, length);
    int rt = m_socket->recv(&iov[0], iov.size());
    if (rt > 0) {
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

int SocketStream::write(const void* buffer, size_t length) {
    if (!isConnected()) {
        return -1;
    }
    return m_socket->send(buffer, length);
}

int SocketStream::write(ByteArray::ptr ba, size_t length) {
    if (!isConnected()) {
        return -1;
    }
    std::vector<iovec> iov;
    ba->getReadBuffers(iov, length);
    int rt = m_socket->send(&iov[0], iov.size());
    if (rt > 0) {
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

void SocketStream::close() {
    if (m_socket) {
        m_socket->close();
    }
}

}