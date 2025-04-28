#include "zlib_stream.h"
#include "sylar/macro.h"

#include <stdexcept>

namespace sylar {

ZlibStream::ptr ZlibStream::CreateGzip(bool encode, uint32_t buff_size = 4096) {
    return Create(encode, buff_size, GZIP);
}

ZlibStream::ptr ZlibStream::CreateZlib(bool encode, uint32_t buff_size = 4096) {
    return Create(encode, buff_size, ZLIB);
}

ZlibStream::ptr ZlibStream::CreateDeflate(bool encode, uint32_t buff_size = 4096) {
    return Create(encode, buff_size, DEFLATE);
}

ZlibStream::ptr ZlibStream::Create(bool encode, uint32_t buff_size = 4096, Type type = DEFLATE,
                                int level = DEFAULT_COMPRESSION, int window_bits = 15,
                                int memlevel = 8, Strategy strategy = DEFAULT) {
    ZlibStream::ptr rt(new ZlibStream(encode, buff_size));
    if (rt->init(type, level, window_bits, memlevel, strategy) == Z_OK) {
        return rt;
    }
    return nullptr;
}

ZlibStream::ZlibStream(bool encode, uint32_t buff_size) 
    : m_bufferSize(buff_size),
      m_encode(encode),
      m_free(true) {
}

ZlibStream::~ZlibStream() {
    if (m_free) {
        for (auto& i : m_buffs) {
            free(i.iov_base);
        }
    }
    if (m_encode) {
        deflateEnd(&m_zstream);
    } else {
        inflateEnd(&m_zstream);
    }
}

int ZlibStream::read(void* buffer, size_t length) {
    throw std::logic_error("ZlibStream::read is invalid");
}

int ZlibStream::read(ByteArray::ptr ba, size_t length) {
    throw std::logic_error("ZlibStream::read is invalid");
}

int ZlibStream::write(const void* buffer, size_t length) {
    //为什么要用iovec，因为它把原来散的两个参数（指针+长度）打包成了一个 iovec 结构体，方便后续统一处理！
    iovec iov;
    iov.iov_base = (void*)buffer;
    iov.iov_len = length;
    if (m_encode) {
        encode(&iov, 1, false);
    } else {
        decode(&iov, 1, false);
    }
}

int ZlibStream::write(ByteArray::ptr ba, size_t length) {
    std::vector<iovec> buffers;
    ba->getReadBuffers(buffers, length);
    if (m_encode) {
        return encode(&buffers[0], buffers.size(), false);
    } else {
        return decode(&buffers[0], buffers.size(), false);
    }
}

void ZlibStream::close() {
    flush();
}

int ZlibStream::init(Type type = DEFLATE, int level = DEFAULT_COMPRESSION, int window_bits = 15,
                     int memlevel = 8, Strategy strategy = DEFAULT) {
    memset(&m_zstream, 0, sizeof(m_zstream));
    //不自定义内存分配/释放函数，使用默认的malloc/free
    m_zstream.zalloc = Z_NULL;
    m_zstream.zfree = Z_NULL;
    m_zstream.opaque = Z_NULL;

    //zlib对不同格式的压缩文件的window_bits有大小限制
    switch (type) {
        case DEFLATE:
            window_bits -= window_bits;
            break;
        case GZIP:
            window_bits += 16;
            break;
        case ZLIB:
        default:
            break;
    }
    if (m_encode) {
        return deflateInit2(&m_zstream, level, Z_DEFLATED, window_bits, memlevel, (int)strategy);
    } else {
        return inflateInit2(&m_zstream, window_bits);
    }
}

int ZlibStream::encode(const iovec* v, const uint64_t& size, bool finish) {
    int ret = 0;
    int flush = 0;
    for (uint64_t i = 0; i < size; ++i) {
        // 我要压缩多少数据（avail_in）   v[i].iov_len 是第i块输入数据的长度，
        // 我要压缩的数据在哪里（next_in） v[i].iov_base 是第i块输入数据的起始地址，
        m_zstream.avail_in = v[i].iov_len;
        m_zstream.next_in = (Bytef*)v[i].iov_base;
        flush = finish ? (i == size - i ? Z_FINISH : Z_NO_FLUSH) : Z_NO_FLUSH;
        iovec* ivc = nullptr;
        do {
            //iov_len表示当前已经写入了多少字节，该判断用于看m_buffs最后一iovec块是否还能写入数据
            if (!m_buffs.empty() && m_buffs.back().iov_len != m_bufferSize) {
                ivc = &m_buffs.back();
            } else {
                iovec iov;
                iov.iov_base = malloc(m_bufferSize);
                iov.iov_len = 0;
                m_buffs.push_back(iov);
                ivc = &m_buffs.back();
            }
            //avail_out 告诉 zlib：我最多还能写多少字节到输出
            //next_out 告诉 zlib：输出数据从哪块内存地址开始写
            m_zstream.avail_out = m_bufferSize - ivc->iov_len;
            m_zstream.next_out = (Bytef*)ivc->iov_base + ivc->iov_len;
            //deflate工作流程：从next_in读取数据(指向传入的v的某块内存)，然后进行压缩，压缩后的数据写入next_out指向的某块内存(m_buffs)
            ret = deflate(&m_zstream, flush);
            if (ret == Z_STREAM_ERROR) {
                return ret;
            }
            ivc->iov_len = m_bufferSize - m_zstream.avail_out;
        } while (m_zstream.avail_out == 0);
    }
    //当完成最后一次压缩时，如果是finish则需要再调用下deflateEnd，来释放内部用来压缩的临时资源
    if (flush == Z_FINISH) {
        deflateEnd(&m_zstream);
    }
    return Z_OK;
}

int ZlibStream::decode(const iovec* v, const uint64_t& size, bool finish) {
    int ret = 0;
    int flush = 0;
    for (uint64_t i = 0; i < size; ++i) {
        m_zstream.avail_in = v[i].iov_len;
        m_zstream.next_in = (Bytef*)v[i].iov_base;
        flush = finish ? (i == size - 1 ? Z_FINISH : Z_NO_FLUSH) : Z_NO_FLUSH;
        iovec* ivc = nullptr;
        do {
            if (!m_buffs.empty() && m_buffs.back().iov_len != m_bufferSize) {
                ivc = &(m_buffs.back());
            } else {
                iovec iov;
                iov.iov_len = 0;
                iov.iov_base = malloc(m_bufferSize);
                m_buffs.push_back(iov);
                ivc = &(m_buffs.back());
            }
            m_zstream.avail_out = m_bufferSize - ivc->iov_len;
            m_zstream.next_out = (Bytef*)ivc->iov_base + ivc->iov_len;
            ret = inflate(&m_zstream, flush);
            if (ret == Z_STREAM_ERROR) {
                return ret;
            }
            ivc->iov_len = m_bufferSize - m_zstream.avail_out;
        } while (m_zstream.avail_out == 0);
    }
    if (flush = Z_FINISH) {
        inflateEnd(&m_zstream);
    }
    return Z_OK;
}

int ZlibStream::flush() {
    //传入空的iov + finish=true表示我不打算再送新数据了，但我要求你把内部剩余的东西处理完
    iovec iov;
    iov.iov_base = nullptr;
    iov.iov_len = 0;
    if (m_encode) {
        return encode(&iov, 1, true);
    } else {
        return decode(&iov, 1, true);
    }
}

std::string ZlibStream::getResult() const {
    std::string rt;
    for (auto& i : m_buffs) {
        rt.append((const char*)i.iov_base, i.iov_len);
    }
    return rt;
}

sylar::ByteArray::ptr ZlibStream::getByteArray() {
    sylar::ByteArray::ptr ba(new ByteArray);
    for (auto& i : m_buffs) {
        ba->write(i.iov_base, i.iov_len);
    }
    ba->setPosition(0);
    return ba;
}

}


