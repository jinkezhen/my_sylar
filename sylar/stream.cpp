#include "stream.h"

namespace sylar {

//实现逻辑是不管底层一次能读/写多少，我循环把交代的长度都处理完为止
//内部的read/write是纯虚函数，需要由子类来实现
int Stream::readFixSize(void* buffer, size_t length) {
    size_t offset = 0;
    int64_t left = length;
    while (left > 0) {
        int64_t len = read((char*)buffer + offset, left);
        if (len < 0) {
            return len;
        }
        offset += len;
        left -= len;
    }
    return length;
}

int Stream::readFixSize(ByteArray::ptr ba, size_t length) {
    int64_t left = length;
    while (left > 0) {
        int64_t len = read(ba, left);
        if (len < 0) {
            return len;
        }
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(const void* buffer, size_t length) {
    size_t offset = 0;
    int64_t left = length;
    while (left > 0) {
        int64_t len = write((const char*)buffer + offset, left);
        if (len < 0) {
            return len;
        }
        offset += len;
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(ByteArray::ptr ba, size_t length) {
    int64_t left = length;
    while (left > 0) {
        int64_t len = write(ba, left);
        if (len < 0) {
            return len;
        }
        left -= len;
    }
    return length;
}

}