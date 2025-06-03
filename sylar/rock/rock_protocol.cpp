#include "rock_protocol.h"
#include "sylar/log.h"
#include "sylar/config.h"
#include "sylar/endian.h"
#include "sylar/streams/zlib_stream.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static sylar::ConfigVar<uint32_t>::ptr g_rock_protocol_max_length
    = sylar::Config::Lookup("rock.protocol.max_length",
                            (uint32_t)(1024 * 1024 * 64), "rock protocol max length");

static sylar::ConfigVar<uint32_t>::ptr g_rock_protocol_gzip_min_length
    = sylar::Config::Lookup("rock.protocol.gzip_min_length",
                            (uint32_t)(1024 * 4), "rock protocol gizp min length");

bool RockBody::serializeToByteArray(ByteArray::ptr bytearray) {
    bytearray->writeStringVint(m_body);
    return true;
}

bool RockBody::parseFromByteArray(ByteArray::ptr bytearray) {
    m_body = bytearray->readStringVint();
    return true;
}

std::shared_ptr<RockResponse> RockRequest::createResponse() {
    RockResponse::ptr rt(new RockResponse);
    rt->setSn(m_sn);
    rt->setCmd(m_cmd);
    return rt;
}

std::string RockRequest::toString() const {
    std::stringstream ss;
    ss << "[RockRequest sn=" << m_sn
       << " cmd=" << m_cmd
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& RockRequest::getName() const {
    static const std::string& s_name = "RockRequest";
    return s_name;
}

int32_t RockRequest::getType() const {
    return Message::REQUEST;
}

bool RockRequest::serializeToByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;

        // 序列化父类 Request 的部分（写入 sn, cmd 等字段）
        v &= Request::serializeToByteArray(bytearray);
        // 序列化 RockBody 的部分（写入 body 字符串）
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockRequest serializeToByteArray error";
    }
    return false;
}

bool RockRequest::parseFromByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;
        // 先从 ByteArray 中读取前面 9 字节（type+sn+cmd）
        // 再读取 body 的长度和内容
        v &= Request::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockRequest parseFromByteArray error";
    }
    return false;
}

std::string RockResponse::toString() const {
    std::stringstream ss;
    ss << "[RockResponse sn=" << m_sn
       << " cmd=" << m_cmd
       << " result=" << m_result
       << " result_msg=" << m_resultStr
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& RockResponse::getName() const {
    static const std::string& s_name = "RockResponse";
    return s_name;
}

int32_t RockResponse::getType() const {
    return Message::RESPONSE;
}

bool RockResponse::serializeToByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;
        v &= Response::serializeToByteArray(bytearray);
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockResponse serializeToByteArray error";
    }
    return false;
}

bool RockResponse::parseFromByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;
        v &= Response::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockResponse parseFromByteArray error";
    }
    return false;
}


std::string RockNotify::toString() const {
    std::stringstream ss;
    ss << "[RockNotify notify=" << m_notify
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& RockNotify::getName() const {
    static const std::string& s_name = "RockNotify";
    return s_name;
}

int32_t RockNotify::getType() const {
    return Message::NOTIFY;
}

bool RockNotify::serializeToByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;
        v &= Notify::serializeToByteArray(bytearray);
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockNotify serializeToByteArray error";
    }
    return false;
}

bool RockNotify::parseFromByteArray(ByteArray::ptr bytearray) {
    try {
        bool v = true;
        v &= Notify::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockNotify parseFromByteArray error";
    }
    return false;
}

Message::ptr RockMessageDecoder::parseFrom(Stream::ptr stream) {
    try {
        RockMsgHeader header;
        if (stream->readFixSize(&header, sizeof(header)) < 0) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder decode head error";
            return nullptr;
        }
        //校验协议标识
        if (memcmp(header.magic, s_rock_magic, sizeof(s_rock_magic))) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.magic error";
            return nullptr;
        }
        //校验版本
        if(header.version != 0x1) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.version != 0x1";
            return nullptr;
        }
        header.length = sylar::byteswapOnLittleEndian(header.length);
        //超过允许的最大长度就拒绝
        if((uint32_t)header.length >= g_rock_protocol_max_length->getValue()) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.length("
                                      << header.length << ") >="
                                      << g_rock_protocol_max_length->getValue();
            return nullptr;
        }
        sylar::ByteArray::ptr ba(new sylar::ByteArray);
        //从流中读取 header.length 个字节填入 ba，确保读满。
        //这里是从stream中的header后的位置开始读，也就是向后读header.length个字节的长度内容
        if(stream->readFixSize(ba, header.length) <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder read body fail length=" << header.length;
            return nullptr;
        }
        //把 ByteArray 的读取指针重置为起始位置，以便后续反序列化。
        ba->setPosition(0);
        if(header.flag & 0x1) { //gizp
            auto zstream = sylar::ZlibStream::CreateGzip(false);
            if(zstream->write(ba, -1) != Z_OK) {
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder ungzip error";
                return nullptr;
            }
            if(zstream->flush() != Z_OK) {
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder ungzip flush error";
                return nullptr;
            }
            ba = zstream->getByteArray();
        }
        //读取消息类型（Request、Response、Notify）
        uint8_t type = ba->readFuint8();
        Message::ptr msg;
        switch(type) {
            case Message::REQUEST:
                msg.reset(new RockRequest);
                break;
            case Message::RESPONSE:
                msg.reset(new RockResponse);
                break;
            case Message::NOTIFY:
                msg.reset(new RockNotify);
                break;
            default:
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder invalid type=" << (int)type;
                return nullptr;
        }
        //调用虚函数反序列化消息体字段（如 sn, cmd, body 等）。
        if(!msg->parseFromByteArray(ba)) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder parseFromByteArray fail type=" << (int)type;
            return nullptr;
        }
        return msg;  
    } catch (std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder except:" << e.what();
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder except";
    }
    return nullptr;
}

int32_t RockMessageDecoder::serializeTo(Stream::ptr stream, Message::ptr msg) {
    RockMsgHeader header;  // 创建消息头结构体，用于构造协议头

    auto ba = msg->toByteArray(); // 将 Message 对象序列化为 ByteArray（二进制消息体）
    ba->setPosition(0);           // 重置读写位置到开头，准备读取数据

    header.length = ba->getSize(); // 设置未压缩前的消息体长度

    // 判断是否需要压缩（消息体长度超过配置的最小压缩长度）
    if((uint32_t)header.length >= g_rock_protocol_gzip_min_length->getValue()) {
        auto zstream = sylar::ZlibStream::CreateGzip(true); // 创建 Gzip 压缩流（true 表示压缩）

        if(zstream->write(ba, -1) != Z_OK) { // 将 ByteArray 数据写入压缩流
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo gizp error";
            return -1;
        }

        if(zstream->flush() != Z_OK) { // 完成压缩，刷新数据
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo gizp flush error";
            return -2;
        }

        ba = zstream->getByteArray();   // 获取压缩后的数据
        header.flag |= 0x1;             // 设置压缩标志位（第 0 位表示 Gzip）
        header.length = ba->getSize();  // 更新压缩后的数据长度
    }

    // 将长度按小端格式存储（保证跨平台一致性）
    header.length = sylar::byteswapOnLittleEndian(header.length);

    // 写入消息头到流中
    if(stream->writeFixSize(&header, sizeof(header)) <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo write header fail";
        return -3;
    }

    // 写入消息体（ByteArray 的实际内容）到流中
    if(stream->writeFixSize(ba, ba->getReadSize()) <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo write body fail";
        return -4;
    }

    // 返回总字节数：消息头 + 消息体
    return sizeof(header) + ba->getSize();
}


}