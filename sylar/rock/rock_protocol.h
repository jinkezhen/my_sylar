#include "sylar/protocol.h"
#include <google/protobuf/message.h>


namespace {

//Rock协议中的通用消息体部分，封装了消息内容字符串（如 protobuf 编码后的字节流）
class RockBody {
public:
    typedef std::shared_ptr<RockBody> ptr;
    virtual ~RockBody(){}

    // 设置消息体内容（通常是已编码的 Protobuf 二进制字符串）
    void setBody(const std::string& v) { m_body = v; }
    // 获取消息体内容（原始字符串）
    const std::string& getBody() const { return m_body; }

    // 下面两个函数用于和网络层之间进行字节级传输。它们其实是网络序列化与反序列化的接口。
    // 将消息体序列化进 ByteArray（写入 body 字符串）
    virtual bool serializeToByteArray(ByteArray::ptr bytearray);
    // 从 ByteArray 中读取消息体内容（读取 body 字符串）
    virtual bool parseFromByteArray(ByteArray::ptr bytearray);

    //消息体反序列化为protbuf对象
    template<class T>
    std::shared_ptr<T> getAdPB() const {
        try {
            std::shared_ptr<T> data(new T);
            if (data->ParseFromString(m_body)) {
                return data;
            }
        } catch (...){}
        return nullptr;
    }

    //将protobuf序列化为字符串存储到m_body中
    template<class T>
    bool setAsPB(const T& v) {
        try {
            return v.SerialzeToString(&m_body);
        } catch (...){}
        return false;
    }

protected:
    std::string m_body;
};

class RockResponse;
//RockRequest表示带有业务数据的请求，继承自Request和RockBody
class RockRequest : public Request, public RockBody {
public:
    typedef std::shared_ptr<RockRequest> ptr;
    
    // 创建响应对象，通常由服务器调用
    // 在服务器收到请求并准备返回响应时，需要生成一个与该请求匹配的 RockResponse，并带上请求的上下文信息（如序列号 sn、命令码 cmd 等）。
    // 这个时候，由 请求对象自己来生成响应对象 是最自然的方式，也最不容易出错。
    std::shared_ptr<RockResponse> createResponse();

    // 获取字符串描述（调试用）
    virtual std::string toString() const override;

    // 获取请求名称（通常用于日志或路由）
    virtual const std::string& getName() const override;

    // 获取请求类型标识，通常为固定整数
    virtual int32_t getType() const override;

    // 将请求序列化进 ByteArray，包含请求头字段和 body 字符串
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;

    // 从 ByteArray 中读取请求内容
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;    

};

// RockResponse 表示带有业务数据的响应，继承自 Response 和 RockBody
class RockResponse : public Response, public RockBody {
public:
    typedef std::shared_ptr<RockResponse> ptr;

    // 获取字符串描述（调试用）
    virtual std::string toString() const override;

    // 获取响应名称（通常用于日志或路由）
    virtual const std::string& getName() const override;

    // 获取响应类型标识，通常为固定整数
    virtual int32_t getType() const override;

    // 将响应序列化进 ByteArray，包含响应头字段和 body 字符串
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;

    // 从 ByteArray 中读取响应内容
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

class RockNotify : public Notify, public RockBody {
public:
    typedef std::shared_ptr<RockNotify> ptr;

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

static const uint8_t s_rock_magic[2] = {0xab, 0xcd};

struct RockMsgHeader {
    RockMsgHeader(); 

    uint8_t magic[2]; // 魔数（Magic Number），用于标识这是一个合法的 ROCK 协议包
                      // 例如可能设定为固定值：0xAB, 0xCD，用于接收端快速判断数据合法性
    uint8_t version;  // 协议版本号，用于兼容升级和版本控制
                      // 比如当前为版本 1，未来若有协议扩展可判断 version 决定解析逻辑
    uint8_t flag;     // 标志位，通常用于控制压缩、加密、是否响应等
                      // 可用位操作表示多个布尔信息（如 bit 0: 是否压缩, bit 1: 是否加密）
    int32_t length;   // 整个消息体（含头部之后的字节数）的长度，单位为字节
                      // 接收端可根据此字段决定读取多少数据构成完整消息
};

//RockMessageDecoder 是 ROCK 协议中消息的“编解码器”，负责网络通信过程中的消息序列化与反序列化，是连接「字节流」与「逻辑消息对象」的桥梁。
class RockMessageDecoder : public MessageDecoder {
public:
    typedef std::shared_ptr<RockMessageDecoder> ptr;

    // 解码函数：从 stream 中读取数据并解析成一个完整 Message（可能是请求或响应）
    virtual Message::ptr parseFrom(Stream::ptr stream) override;
    // 编码函数：将 msg 序列化后写入到 stream 中
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) override;
};


}