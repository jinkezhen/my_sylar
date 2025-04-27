#include "bytearray.h"
#include "endian.h"
#include "log.h"

#include <stdexcept>
#include <string.h>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ByteArray::Node::Node(size_t s)
    : ptr(new char[s]),
      next(nullptr),
      size(0) {
}

ByteArray::Node::Node() 
    : ptr(nullptr),
      next(nullptr),
      size(0) {
}

ByteArray::Node::~Node() {
    if (ptr) {
        delete[] ptr;
    }
}

ByteArray::ByteArray(size_t base_size) :
    m_baseSize(base_size),
    m_position(0),
    m_capacity(base_size),
    m_size(0),
    m_endian(SYLAR_BIG_ENDIAN),
    m_root(new Node(base_size)),
    m_cur(m_root) {
}

ByteArray::~ByteArray() {
    Node* temp = m_root;
    while (temp) {
        m_cur = temp;
        temp = temp->next;
        delete m_cur;
    }
}

void ByteArray::clear() {
    m_position = m_size = 0;
    m_capacity = m_baseSize;
    Node* temp = m_root->next;
    while (temp) {
        m_cur = temp;
        temp = temp->next;
        delete m_cur;
    }
    m_cur = m_root;
    m_root->next == nullptr;
}

bool ByteArray::isLittleEndian() const {
    return m_endian == SYLAR_LITTLE_ENDIAN;
}

void ByteArray::setIsLittleEndian(bool val) {
    if (val) {
        m_endian = SYLAR_LITTLE_ENDIAN;
    } else {
        m_endian = SYLAR_BIG_ENDIAN;
    }
}

void ByteArray::addCapacity(size_t size) {
    if (size == 0) return;
    size_t old_cap = getCapacity();
    if (old_cap >= size) return;

    //计算缺口大小
    size -= old_cap;
    //计算需要增加多少个节点，×浮点数后向上取整
    size_t count = ceil(1.0 * (size / m_baseSize));

    Node* temp = m_root;
    //找到尾节点
    while (temp->next) {
        temp = temp->next;
    }
    //用于记录新添加节点的第一个
    Node* first = nullptr;
    //添加新节点
    for (size_t i = 0; i < count; ++i) {
        temp -> next = new Node(m_baseSize);
        if (first == nullptr) {
            first = temp -> next;
        }
        temp = temp->next;
        m_capacity += m_baseSize;
    }
    //如果在扩容之前容量为0，那么更新m_cur为扩容后的第一个块
    if (old_cap == 0) {
        m_cur = first;
    }
}

void ByteArray::setPosition(size_t v) {
    if (v > m_capacity) {
        return throw out_of_range("set position out of range");
    }
    m_position = v;
    if (m_position > m_size) {
        m_size = m_position;
    }
    m_cur = m_root;
    while (v > m_cur->size) {
        v -= m_cur->size;
        m_cur = m_cur->next;
    }
    if (v == m_cur->size) {
        m_cur = m_cur->next;
    }
}

//将一段原始内存写入到ByteArray中，并根据需要扩展容量，维护当前位置和总长度等状态
void ByteArray::write(const void* buf, size_t size) {
    if (size < 0) return;
    addCapacity(size);
    size_t npos = m_position % m_baseSize;   //npos：当前写入位置在当前块node中的偏移(即块内的偏移)
    size_t ncap = m_cur->size - npos;        //ncap：当前块从偏移位置开始还能写入的字节数，即当前快中的剩余空间
    size_t bpos = 0;                         //bpos：源缓冲区buf中的偏移量，用于追踪从哪里开始复制数据
    while (size > 0) {
        //如果当前块可以一次性写完这size大小的数据
        if (ncap >= size) {
            memcpy(m_cur->ptr + npos, (const void*)buf + bpos, size);
            if (m_cur->size == (npos + size)) {
                m_cur = m_cur->next;
            }
            m_position += size;
            bpos += size;
            size = 0;
        } else {
            memcpy(m_cur->ptr + npos, (const void*)buf, ncap);
            m_position += ncap;
            size -= ncap;
            bpos += ncap;

            m_cur = m_cur->next;
            npos = 0;
            ncap = m_cur->size;
        }
    }
    if (m_position > m_size) {
        m_size = m_position;
    }
}

//从当前ByteArray读取size大小的数据，存储到buf中
void ByteArray::read(void* buf, size_t size) {
    if (size > getReadSize()) {
        throw std::out_of_range("not enough len");
    }
    size_t npos = m_position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    size_t bpos = 0;
    while (size > 0) {
        if (ncap >= size) {
            memcpy((char*)buf + bpos, m_cur->ptr + npos, size);
            if (m_cur->size == npos + size) {
                m_cur = m_cur->next;
            }
            m_position += size;
            bpos += size;
            size = 0;
        } else {
            memcpy((char*)buf + bpos, m_cur->ptr + npos, ncap);
            m_position += ncap;
            bpos += ncap;
            size -= ncap;
            m_cur = m_cur->next;
            ncap = m_cur->size;
            npos = 0;
        }
    }
}

//从ByteArray指定位置开始读取size个数据，并存入buf
void ByteArray::read(void* buf, size_t size, size_t position) {
    if (size > (m_size - position)) {
        throw std::out_of_range("not enough len");
    }
    size_t node_index = position / m_baseSize;
    Node* cur = m_root;
    while (node_index) {
        cur = cur->next;
        node_index--;
    }
    size_t npos = position % m_baseSize;
    size_t ncap = cur->size - npos;
    size_t bpos = 0;

    while (size > 0) {
        if (ncap >= size) {
            memcpy((char*)buf + bpos, cur->ptr + npos, size);
            if (cur->size == npos + size) {
                cur = cur->next;
            }
            m_position += size;
            bpos += size;
            size = 0;
        } else {
            memcpy((char*)buf + bpos, cur->ptr + npos, ncap);
            m_position += ncap;
            bpos += ncap;
            size -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
    }
}

std::string ByteArray::toString() const {
    std::string str;
    str.resize(getReadSize());
    if (str.empty()) return str;
    read(&str[0], str.size(), m_position);
    return str;
}

//str = hello
//return "47 86 a7 c8 87"
std::string ByteArray::toHexString() const {
    std::string str = toString();
    std::stringstream ss;
    for (size_t i = 0; i < str.size(); ++i) {
        if (i > 0 && i % 32 == 0) {//每隔32个换一行 
            ss << std::endl;
        }
        ss << std::setw(2)         //设置宽度为2，不足两位会用前导字符补齐
           << std::setfill('0')    //补齐的字符用0
           << std::hex             //切换为16进制
           << (int)(uint8_t)str[i] //转为无符号整数
           << " "; 
    }
    return ss.str();
}


//写入固定长度的数据
//写入一字节无需字节序转换
void ByteArray::writeFint8(int8_t value) {
    write(&value, sizeof(value));
}
void ByteArray::writeFuint8(uint8_t value) {
    write(&value, sizeof(value));
}
void ByteArray::writeFint16(int16_t value) {
    //如果设置的字节序与系统原生不同，就要字节序变换
    if (m_endian != SYLAR_BYTE_ORDER) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}
void ByteArray::writeFuint16(uint16_t value) {
    if (m_endian != SYLAR_BYTE_ORDER) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}
void ByteArray::writeFint32(int32_t value) {
    if (m_endian != SYLAR_BYTE_ORDER) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}
void ByteArray::writeFuint32(uint32_t value) {
    if (m_endian != SYLAR_BYTE_ORDER) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}
void ByteArray::writeFloat(float value) {
    //memcpy用于在内存中按字节拷贝数据，并不关心数据类型，只要这个两个数据区域的字节大小匹配即可
    uint32_t v;
    memcpy(&v, &value, sizeof(value));
    writeFuint32(v);
}
void ByteArray::writeDouble(double value) {
    uint64_t v;
    memcpy(&v, &value, sizeof(value));
    writeFint64(v);
}
//读固定长度的数据
int8_t ByteArray::readFint8() {
    int8_t v;
    read(&v, sizeof(v));
    return v;
}
uint8_t ByteArray::readFuint8() {
    uint8_t v;
    read(&v, sizeof(v));
    return v;
}

#define XX(type)                           \
    type v;                                \
    read(&v, sizeof(v));                   \
    if (m_endian != SYLAR_BYTE_ORDER) {    \
        return byteswap(v);                \
    } else {                               \
        return v;                          \
    }
int16_t ByteArray::readFint16() {
    XX(int16_t);
}
uint16_t ByteArray::readFuint16() {
    XX(uint16_t);
}
int32_t ByteArray::readFint32() {
    XX(int32_t);
}
uint32_t ByteArray::readFuint32() {
    XX(uint32_t);
}
int64_t ByteArray::readFint64() {
    XX(int64_t);
}
uint64_t ByteArray::readFuint64() {
    XX(uint64_t);
}
#undef XX
float ByteArray::readFloat() {
    uint32_t v = readFuint32();
    float value;
    memcpy(&value, &v, sizeof(v));
    return value;
}
double ByteArray::readDouble() {
    uint64_t v = readFuint64();
    double value;
    memcpy(&value, &v, sizeof(v));
    return value;
}


//向ByteArray中写入字符串，连带写入字符串的长度用指定的数据类型
void ByteArray::writeStringF16(const std::string& value) {
    writeFuint16(value.size());
    write(value.c_str(), value.size());
}
void ByteArray::writeStringF32(const std::string& value) {
    writeFuint32(value.size());
    write(value.c_str(), value.size());
}
void ByteArray::writeStringF64(const std::string& value) {
    writeFuint64(value.size());
    write(value.c_str(), value.size());
}
void ByteArray::writeStringVint(const std::string& value) {
    writeUint64(value.size());
    write(value.c_str(), value.size());
}
void ByteArray::writeStringWithoutLength(const std::string& value) {
    write(value.c_str(), value.size());
}
//从ByteArray中读取字符串指定大小的字符串内容
std::string ByteArray::readStringF16() {
    uint16_t len = readFuint16();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}
std::string ByteArray::readStringF32() {
    uint32_t len = readFuint32();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}
std::string ByteArray::readStringF64() {
    uint64_t len = readFuint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}
std::string ByteArray::readStringVin() {
    uint64_t len = readUint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}


//以下四个函数是ZigZag编码和解码函数，它们用于将有符号整数(int32_t int64_t)转为无符号整数(uint32_t uint64_t)
//以及从无符号整数还原为有符号整数，ZigZag编码在protobuf等序列化中非常有用，用于更高效的表示负数
//int32_t->uint32_t
static uint32_t EncodeZigzag32(const int32_t& v) {
    //负数映射为奇数，整数映射为偶数
    // 0  ---  0
    // -1 ---  1
    // 1  ---  2
    // -2 ---  3
    // 2  ---  4
    if (v < 0) {
        return (uint32_t)(-v)*2 - 1;
    } else {
        return v * 2;
    }
}
//int64_t->uint64_t
static uint64_t EncodeZigzag64(const int64_t& v) {
    if (v < 0) {
        return (uint32_t)(-v)*2 - 1;
    } else {
        return v * 2;
    }
}
//uint32_t->int32_t
static int32_t DecodeZigzag32(const uint32_t& v) {
    //v >> 1相当于除2
    //v & 1取最低位置，如果v为奇数，则最低位为1，得到的结果为1；如果v为偶数，则最低位为0，得到的结果为0
    //-(v&1)，奇数对应-1，偶数对应0,因为编码时将负数编码为了奇数，正数编码为偶数
    //a^0=a  a^a=0 a^(全1)=a各位取反
    //-1的补码表示是全1，所以一个数^-1时会将该数的所有位都取反，5^-1=-6   7^-1=-8
    return (v >> 1) ^ -(v & 1);
}
//uint64_t->int64_t
static int64_t DecodeZigzag64(const uint64_t& v) {
    return (v >> 1) ^ -(v & 1);
}


//将数据以变长编码方式写入ByteArray
//所有有符号的先转换为无符号的
void ByteArray::writeInt32(int32_t value) {
    writeUint32(EncodeZigzag32(value));
}
//将一个无符号的32位整数value按Varint变长编码的方式压缩成1-5个字节写入到字节数组
void ByteArray::writeUint32(uint32_t value) {
    //对于32位的来说，做多需要的字节不超过5个，算上每个字节的标志位最多可能到达5个字节
    uint8_t temp[5];
    uint8_t i = 0;
    //0x80是1000 0000,如果大于等于这个数说明还得再需要一个字节(因为我们要取低7位加一个标志位，取完低7位还剩一个1)，
    //如果是0111 1111及以下则不再需要多一个字节存储
    while (value >= 0x80) {
        //取第7位并将最高位置1后以一个字节的长度存入temp
        temp[i++] = (0x7F & value) | 0x80;
        //value右移7位
        value >> 7;
    }
    temp[i++] = value;
    write(temp, i);
}
void ByteArray::writeInt64(int64_t value) {
    writeUint64(EncodeZigzag64(value));
}
void ByteArray::writeUint64(uint64_t value) {
    uint8_t temp[10];
    uint8_t i = 0;
    while (value >= 0x80) {
        temp[i++] = (0x7F & value) | 0x80;
        value >> 7;
    }
    temp[i++] = value;
    write(temp, i);
}
int32_t ByteArray::readInt32() {
    return DecodeZigzag32(readUint32());
}
uint32_t ByteArray::readUint32() {
    uint32_t result = 0;
    for (int i = 0; i < 32; i += 7) {
        uint8_t b = readFuint8();
        if (b < 0x80) {
            result |= (uint32_t)b << i;
            break;
        } else {
            result |= (((uint32_t)(b & 0x7F)) << i);
        }
    }
    return result;
}
int64_t ByteArray::readInt64() {
    return DecodeZigzag64(readUint64());
}
uint64_t ByteArray::readUint64() {
    uint64_t result = 0;
    for (int i = 0; i < 32; i += 7) {
        uint8_t b = readFuint8();
        if (b < 0x80) {
            result |= (uint64_t)b << i;
            break;
        } else {
            result |= (((uint64_t)(b & 0x7F)) << i);
        }
    }
    return result;
}

//将ByteArray中从当前位置(m_position)开始的所有可读数据写入到名为name的文件中
bool writeToFile(const std::string& name) const {
    std::ofstream ofs;
    ofs.open(name, std::ios::trunc | std::ios::binary);
    if (!ofs) {
        return false;
    }
    int64_t read_size = getReadSize();
    int64_t pos = m_position;
    Node* cur = m_cur;
    while (read_size > 0) {
        //当前块中已读取的偏移量
        int diff = pos % m_baseSize;
        int64_t len = (read_size > (int64_t)m_baseSize ? m_baseSize : read_size) - diff;
        ofs.write(cur->ptr + diff, len);
        cur = cur->next;
        pos += len;
        read_size -= len;
    }
    return true;
}
//从一个二进制文件中读取数据，并写入当前ByteArray
bool readFromFile(const std::string& name) const {
    std::ifstream ifs;
    ifs.open(name, std::ios::binary);
    if (!ifs) return false;
    //创建缓冲区，用于每次读取数据
    std::shared_ptr<char> buff(new char[m_baseSize], [](char* ptr) {delete[] ptr});
    while (!ifs.eof()) {  //没读到末尾
        //从文件中最多读取m_baseSize个字节到缓冲区
        ifs.read(buff.get(), m_baseSize);
        //把读取到buff中的数据写入到byteArray中
        write(buff.get(), ifs.gcount());
    }
    return true;
}


//下面的三个函数都是配合可以收发iovec的函数使用如readv、writev
uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len) const {
    len = (len > getReadSize()) ? getReadSize() : len;
    if (len == 0) return;
    uint64_t size = len;
    size_t npos = m_position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    struct iovec iov;
    Node* cur = m_cur;
    while (len > 0) {
        if (ncap > len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            ncap = cur->size;
            cur = cur->next;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const {
    len = (len > getReadSize()) ? getReadSize() : len;
    if (len == 0) return;
    uint64_t size = len;
    size_t npos = position % m_baseSize;
    size_t count = position / m_baseSize;
    Node* cur = m_root;
    while (count > 0) {
        cur = cur->next;
        count--;
    }
    size_t ncap = cur->size - npos;
    struct iovec iov;
    while (len > 0) {
        if (ncap > len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            ncap = cur->size;
            cur = cur->next;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getWriteBuffers(std::vector<iovec>& buffers, uint64_t len) const {
    if (len == 0) return 0;
    addCapacity(len);  //确保有足够的容量写入len字节的数据
    uint64_t size = len;
    size_t npos = m_position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    struct iovec iov;
    Node* cur = m_cur;
    while (len > 0) {
        if (ncap > len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            ncap = cur->size;
            cur = cur->next;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}


}
