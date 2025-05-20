//该文件定义了一个基于zlib压缩库的流处理类ZlibStream，支持对数据进行压缩和解压缩
//并并封装了常用的 zlib 格式（ZLIB、DEFLATE、GZIP）以及压缩级别、策略等设置，
//提供了读、写、关闭、刷新等接口，同时内部管理压缩/解压状态和缓存，方便在网络传输或
//文件处理中对数据流进行透明压缩处理。


#ifndef __SYLAR_ZLIB_STREAM_H__
#define __SYLAR_ZLIB_STREAM_H__

#include "sylar/stream.h"
#include "bytearray.h"

#include <memory>
#include <vector>
#include <string>
#include <stdint.h>
#include <sys/uio.h>  //iovec

#include <zlib.h>

namespace sylar {

class ZlibStream : public Stream {
public:
    typedef std::shared_ptr<ZlibStream> ptr;

    //压缩/解压文件的标准
    enum Type {
        ZLIB,
        DEFLATE,
        GZIP
    };

    //指定压缩/解压文件时的策略，直接对应zlib里的压缩策略选项
    //根据数据特性选择最合适的压缩方式
    enum Strategy {
        DEFAULT,    //默认策略
        FILTERED,   //过滤器策略，适合很多小变化的数据，如图像数据
        HUFFMAN,    //只使用哈夫曼编码，压缩快，但压缩率低
        FIXED,      //私用固定哈夫曼编码
        RLE         //只做游程编码
    };

    //用来指定压缩级别，主要控制压缩的有多紧、压缩的有多快
    enum CompressLevel {
        NO_COMPRESSION = Z_NO_COMPRESSION,          //不压缩直接原样输出，不压缩，只打包，最快，但没节省空间。
        BSET_SPEED = Z_BEST_SPEED,                  //最快压缩,尽量减少压缩计算量，压缩速度最快，但压缩率比较低。
        BEST_COMPRESSION = Z_BEST_COMPRESSION,      //最好压缩,尽量把数据压得最小，但会非常慢，计算量大。
        DEFAULT_COMPRESSION = Z_DEFAULT_COMPRESSION //默认压缩,在压缩率和速度之间平衡，适合大多数情况。
    };

    //create：快速创建处理不同格式(gzip、zlib、deflate)的ZlibStream对象
    //bool encode：选择压缩还是解压缩
    //buff_size：内部缓冲区大小
    static ZlibStream::ptr CreateGzip(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateZlib(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateDeflate(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr Create(bool encode, uint32_t buff_size = 4096, Type type = DEFLATE,
                                 int level = DEFAULT_COMPRESSION, int window_bits = 15,
                                 int memlevel = 8, Strategy strategy = DEFAULT);
    ZlibStream(bool encode, uint32_t buff_size = 4096);
    ~ZlibStream();

    //从ZlibStream中读出解压/压缩后的数据到普通缓冲区，每次read拿到的都是一块已经压缩/解压好的原始数据
    virtual int read(void* buffer, size_t length) override;
    virtual int read(ByteArray::ptr ba, size_t length) override;
    //把buffer中的数据写入到ZlibStream中，
    //如果是压缩模式就会将buffer中的数据压缩后存入到ZlibStream缓冲区中
    //如果是解压模式就会将buffer中的数据解压后存入到ZlibStream缓冲区中
    virtual int write(const void* buffer, size_t length) override;
    virtual int write(ByteArray::ptr ba, size_t length) override;
    //关闭ZlibStream释放资源
    virtual void close() override;

    //强制刷新当前压缩/解压的缓冲区
    //对于zlib来说在压缩时，压缩的数据可能会先存储在缓冲区中(zlib库内部自己维护的一块内存区域)，直到达到一定大小才输出
    //flush会清空缓冲区并输出所有已压缩/解压的数据，即使当前内存大小没有被填满
    //即该函数用来立即输出缓冲区的内容，确保不会有任何数据被滞留在内存冲
    //一句话总结：将zlib缓冲区中还没处理的数据强制处理完
    int flush();
    
    bool isFree() const { return m_free; }
    void setFree(bool v) { m_free = v; }
    bool isEncode() const { return m_encode; }
    void setEncode(bool v) { m_encode = v; }
    std::vector<iovec>& getBuffers() { return m_buffs; }
    //获取当前ZlibStream中已经压缩或这解压的数据，以字符串形式返回
    std::string getResult() const;
    //获取当前ZlibStream中已经压缩或这解压的数据，以ByteArray形式返回
    sylar::ByteArray::ptr getByteArray();

private:
    //初始化zlib的内部状态(m_zstream)
    int init(Type type = DEFLATE, int level = DEFAULT_COMPRESSION, int window_bits = 15,
             int memlevel = 8, Strategy strategy = DEFAULT);
    //对输入的数据进行压缩处理
    //v：传入一组 iovec（一般是多个 buffer），代表要压缩的原始数据。
    //size：这些数据的总长度。
    //finish：
    // 如果是true，说明你要告诉 zlib：这是最后一批数据了，可以打包压缩尾部。
    // 如果是false，说明后面还会继续喂数据过来，先暂时压缩当前这批。
    int encode(const iovec* v, const uint64_t& size, bool finish);
    //对输入的数据进行解压处理
    int decode(const iovec* v, const uint64_t& size, bool finish);

private:
    z_stream m_zstream;
    //内部缓冲区大小：指定每次压缩/解压内部最多处理大多的数据
    uint32_t m_bufferSize;
    //标记当前是压缩模式还是解压模式
    bool m_encode;
    //控制zlib内部资源是否可以自动释放
    //在某些情况下，比如自己控制z_stream的生命周期时可能不希望自动释放zlib内部某些资源，此时可以自己释放
    //默认是true，也就是会在析构时自动释放zlib资源
    bool m_free;
    //保存压缩或解压结果的数据块
    std::vector<iovec> m_buffs;
};


}

#endif