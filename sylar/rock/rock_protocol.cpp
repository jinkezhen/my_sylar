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


}