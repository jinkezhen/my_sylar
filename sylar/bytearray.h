/**
 * @file bytearray.h
 * @brief 二进制数组(序列化、反序列化)
 * @date 2025-04-07
 * @copyright Copyright (c) 2025年 All rights reversed
 */

//byteArray这个类的本质作用：读写各种类型的数据(int, float, string)到一个byte缓冲区中，
//                          或者从这个byte缓冲区中读取数据并还原成变量
//byteArray是一个可变长度的二进制数据容器，可以用它读写任意类型的数据

#ifndef __SYLAR_BYTEARRAY_H__
#define __SYLAR_BYTEARRAY_H__

#include <memory>
#include <vector>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

namespace sylar {

class ByteArray {
public:
    typedef std::shared_ptr<ByteArray> ptr;

    //ByteArray的底层不是一个大数组，而是一串链表形式的内存块(node->node->node......)
    //node是ByteArray用来自定义内存块的结构体，设计成链表结构是为了避免一次性分配大块连续内存，
    //提升内存管理的灵活性和效率。每个节点表示一块内存，满了就自动添加新节点，方便动态扩容
    //适合处理数据流等场景。这种设计兼顾了性能与灵活性
    struct Node {
        //构造指定大小的内存块
        Node(size_t s);
        Node();
        ~Node();

        //该内存块的地址指针，指向这一块内存中实际存放数据的首地址
        char* ptr;
        //下一个内存块地址
        Node* next;
        //该内存块的大小
        size_t size;
    };

    //使用指定长度的内存块构造ByteArray,默认是4096(4kb)，内存页默认大小就是4096字节
    //即每个node节点的内存默认大小是4kb
    ByteArray(size_t base_size = 4096);
    ~ByteArray();

    //读取、写入固定长度(某个类型的长度)的数据：编码解码速度快，但空间效率低，对于小数据来说不节省空间
    //比如int32_t a = 1;实际只占用一字节，
    //ByteArray ba; ba.writeFint32(a);
    //但实际写入的是sizeof(int32_tint32_t)，即4字节的数据(00 00 00 01)
    void writeFint8(int8_t value);
    void writeFuint8(uint8_t value);
    void writeFint16(int16_t value);
    void writeFuint16(uint16_t value);
    void writeFint32(int32_t value);
    void writeFuint32(uint32_t value);
    void writeFint64(int64_t value);
    void writeFuint64(uint64_t value);
    void writeFloat(float value);   //float占用4字节 32位
    void writeDouble(double value); //double占用8字节 64位
    int8_t readFint8();
    uint8_t readFuint8();
    int16_t readFint16();
    uint16_t readFuint16();
    int32_t readFint32();
    uint32_t readFuint32();
    int64_t readFint64();
    uint64_t readFuint64();
    float readFloat();
    double readDouble();

    //读取、写入长度不固定的数据(具体写入)：实现变长整数编码(varint)，空间效率高，但编码解码效率低
    //varint编码原理：每个字节用7位表示数值，最高位作为是否还有后续字节的标志位
    //如果最高位是1，表示后面还有字节；如果最高位是0，说明已经结束
    //示例：300二进制: 0000 0001 0010 1100
    //varint编码为：0000 0010（0 + 第8位-第14位）   1010 1100（1 + 低7位）
    //所以最终只需要两个字节存储
    //
    //
    //比如int32_t a = 1;实际只占用一字节，
    //ByteArray ba; ba.writeInt32(a);
    //实际写入的是1字节的数据(01)
    //
    //对于读取(解码)来说：每次读取一个字节，一次检查最高位(第八位)，
    //如果是1说明后面还有数据，如果是0说明是最后一个，去掉每个字节的最高位，
    //把剩下的7位拼接起来就是数值
    void writeInt32(int32_t value);
    void writeUint32(uint32_t value);
    void writeInt64(int64_t value);
    void writeUint64(uint64_t value);
    int32_t readInt32();
    uint32_t readUint32();
    int64_t readInt64();
    uint64_t readUint64();

    //写入字符串：他们都是把std::string写入ByteArray，区别在于是否写入字符串长度，以及长度是怎么存的
    //用int6_t记录字符串的长度，记录的长度连同实际内容一起写入到ByteArray中,适合字符串长度小于2的16次方的
    //示例：std::string s = "hello"; writeStringF16(s);实际写入的是[00 05][68 65 73 92 32]
    void writeStringF16(const std::string& value);
    void writeStringF32(const std::string& value);
    void writeStringF64(const std::string& value);
    void writeStringVint(const std::string& value); //使用变长的数据表示字符串长度
    void writeStringWithoutLength(const std::string& value); //直接写入字符串内容，不写长度，需要在别处直到字符串多长，不然读取时不知道怎么断开
    std::string readStringF16();  //先读取2字节，读取到的内容是接下来要读取的字符串的长度
    std::string readStringF32();
    std::string readStringF64();
    std::string readStringVint();

    //判断是否是小端模式
    bool isLittleEndian() const;
    //是否设置为小端模式
    void setIsLittleEndian(bool val);

    //清空ByteArray
    void clear();

    //向当前ByteArray写入size长度的数据
    //buf：内存缓冲区指针
    //size：数据大小
    void write(const void* buf, size_t size);
    //从当前ByteArray读取size长度的数据
    //buf：内存缓冲区指针
    //size：数据大小
    void read(void* buf, size_t size);
    //从postion位置开始读取size长度的数据
    void read(void* buf, size_t size, size_t position);

    //设置ByteArray的当前位置
    void setPosition(size_t v);
    size_t getPosition() const { return m_position; }
    //返回可读数据大小
    size_t getReadSize() const { return m_size - m_position; }
    //返回数组的长度
    uint64_t getSize() const {return m_size;}
    //返回内存块的大小
    size_t getBaseSize() const {return m_baseSize;}

    //将ByteArray中的数据[m_position,m_size)转为std::string
    std::string toString() const;
    //将ByteArray中的数据[m_position,m_size)转为16进制的std::string(格式：FF FF FF)
    std::string toHexString() const;

    //将bytearray中的数据写入到文件中
    bool writeToFile(const std::string& name) const;
    //从文件中读取数据写入到bytearray中
    bool readFromFile(const std::string& name) const;

    //注意iov实际存储的是内存块的地址和长度，而不是具体内容
    //获取可读取的缓冲区，保存成iovec数组
    //len：读取数据的长度
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len = ~0ull) const;
    //从position位置开始获取可读取的缓冲区，保存成iovec数组
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len = ~0ull) const;
    //将ByteArray中当前的可写空间(还没有写入数据的空间)组成iovec,存入buffers中，供后续高效写入使用(如writev函数)
    //写入的长度
    uint64_t getWriteBuffers(std::vector<iovec>& buffers, uint64_t len) const;


private:
    //扩容ByteArray使其可以容纳size个数据(如果原本可以容纳，则不扩容)
    void addCapacity(size_t size);
    size_t getCapacity() const {return m_capacity - m_position;}

private:
    //内存块的基础大小，表示每一个小块(node)的大小
    size_t m_baseSize;
    //以整个ByteArray为单位(不是当前块的位置)时的当前读写位置
    size_t m_position;
    //总容量，表示当前所有已分配的内存块总和(以byte为单位)
    size_t m_capacity;
    //实际有效数据的大小,表示真正写入了多少数据
    size_t m_size;
    //字节序
    int8_t m_endian;
    //第一个内存块
    Node* m_root;
    //当前操作的内存块
    Node* m_cur;
};



}


#endif
