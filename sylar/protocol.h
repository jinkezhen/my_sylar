/**
 * @file protocol.h
 * @brief 自定义协议
 * @date 2025-06-02
 * @copyright Copyright (c) 2025年 All rights reserved
 */

#ifndef __SYLAR_PROTOCOL_H__
#define __SYLAR_PROTOCOL_H__

#include <memory>
#include "sylar/stream.h"
#include "sylar/bytearray.h"

namespace sylar {

/**
 * @brief 抽象消息基类，表示RPC通信中的消息数据结构
 * 包含请求(Request)、响应(Response)、通知(Notify)三种消息类型的公共接口
 * 所有消息均支持序列化和反序列化，方便网络传输
 */
class Message {
public:
    typedef std::shared_ptr<Message> ptr;

    enum MessageType {
        REQUEST = 1,
        RESPONSE = 2,
        NOTIFY = 3
    };

    virtual ~Message() {}

    virtual ByteArray::ptr toByteArray();
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) = 0;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) = 0;

    //消息转换为可读的字符串
    virtual std::string toString() const = 0;
    virtual const std::string& getName() const = 0;
    virtual int32_t getType() const = 0;
};

/**
 * @brief 消息解码器基类。用于从流中解析消息和将消息写入流
 * 不同协议会有不同的实现，完成流到消息对象的转换和反向操作
 */
class MessageDecoder {
public:
    typedef std::shared_ptr<MessageDecoder> ptr;
    virtual ~MessageDecoder(){}

    /**
     * @brief 从流中解析消息，返回对应的Message智能指针
     * @param stream 数据流对象，包含网络读取的数据
     * @return Message::ptr 解析得到的消息对象
     */
    virtual Message::ptr parseFrom(Stream::ptr stream) = 0;
    /**
     * @brief 将消息序列化写入到流中
     * @param stream 目标数据流
     * @param msg 待序列化的消息对象
     * @return int32_t 写入字节数或错误码
     */
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) = 0;
};

/**
 * @brief 请求消息类，继承自Message，表示客户端请求服务器调用的消息
 * 
 * 包含请求编号(m_sn)和命令码(m_cmd)两部分，用于唯一标识请求和调用具体的命令。
 */
class Request : public Message {
public:
    typedef std::shared_ptr<Request> ptr;

    Request();

    uint32_t getSn() const { return m_sn;}
    uint32_t getCmd() const { return m_cmd;}

    void setSn(uint32_t v) { m_sn = v; }
    void setCmd(uint32_t v) { m_cmd = v; }

    /**
     * @brief 将请求内容序列化写入字节数组
     * @param bytearray 目标字节数组
     * @return bool 是否成功
     */
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;

    /**
     * @brief 从字节数组中反序列化请求内容
     * @param bytearray 源字节数组
     * @return bool 是否成功
     */
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t m_sn;  //唯一请求标号，常用于对应响应
    uint32_t m_cmd; //请求的命令码，用于区分具体调用的接口
};

/**
 * @brief 响应消息类，继承自Message，表示服务器返回给客户端的响应结果
 * 
 * 除了请求编号和命令码外，还包含结果码(m_result)和结果描述字符串(m_resultStr)。
 */
class Response : public Message {
public:
    typedef std::shared_ptr<Response> ptr;

    Response();

    uint32_t getSn() const { return m_sn;}               ///< 获取响应对应的请求编号
    uint32_t getCmd() const { return m_cmd;}             ///< 获取响应对应的命令码
    uint32_t getResult() const { return m_result;}       ///< 获取响应结果码，0通常表示成功
    const std::string& getResultStr() const { return m_resultStr;} ///< 获取结果描述信息

    void setSn(uint32_t v) { m_sn = v;}                  ///< 设置响应编号
    void setCmd(uint32_t v) { m_cmd = v;}                ///< 设置响应命令码
    void setResult(uint32_t v) { m_result = v;}          ///< 设置响应结果码
    void setResultStr(const std::string& v) { m_resultStr = v;} ///< 设置响应结果描述

    /**
     * @brief 序列化响应内容写入字节数组
     * @param bytearray 目标字节数组
     * @return bool 是否成功
     */
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;

    /**
     * @brief 从字节数组中反序列化响应内容
     * @param bytearray 源字节数组
     * @return bool 是否成功
     */
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t m_sn;     //响应对应的请求序号
    uint32_t m_cmd;    //响应对应的命令码
    uint32_t m_result; //相应的结果码(状态码)
    std::string m_resultStr;  //响应结果描述信息
};

/**
 * @brief 通知消息类，继承自Message，表示服务器主动发送的通知消息
 * 
 * 只包含通知的标识字段(m_notify)，用于客户端订阅的事件推送。
 */
/**
 * @brief 通知消息类，继承自Message，表示服务器主动发送的通知消息
 * 
 * 只包含通知的标识字段(m_notify)，用于客户端订阅的事件推送。
 */
class Notify : public Message {
public:
    typedef std::shared_ptr<Notify> ptr;
    Notify();

    uint32_t getNotify() const { return m_notify;}   ///< 获取通知标识码
    void setNotify(uint32_t v) { m_notify = v;}      ///< 设置通知标识码

    /**
     * @brief 序列化通知内容写入字节数组
     * @param bytearray 目标字节数组
     * @return bool 是否成功
     */
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;

    /**
     * @brief 从字节数组中反序列化通知内容
     * @param bytearray 源字节数组
     * @return bool 是否成功
     */
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t m_notify; ///< 通知标识码
};


}

#endif