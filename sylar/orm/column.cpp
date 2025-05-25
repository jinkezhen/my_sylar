#include "utit.h"
#include "column.h"
#include "sylar/log.h"


namespace syalr {
namespace orm {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("orm");

Column::Type Column::ParseType(const std::string& v) {
#define XX(a, b, c) \
    if (#b == v) {  \
        return a;   \
    } else if (#c == v) { \
        return a;  \
    }

    XX(TYPE_INT8, int8_t, int8);
    XX(TYPE_UINT8, uint8_t, uint8);
    XX(TYPE_INT16, int16_t, int16);
    XX(TYPE_UINT16, uint16_t, uint16);
    XX(TYPE_INT32, int32_t, int32);
    XX(TYPE_UINT32, uint32_t, uint32);
    XX(TYPE_FLOAT, float, float);
    XX(TYPE_INT64, int64_t, int64);
    XX(TYPE_UINT64, uint64_t, uint64);
    XX(TYPE_DOUBLE, double, double);
    XX(TYPE_STRING, string, std::string);
    XX(TYPE_TEXT, text, std::string);
    XX(TYPE_BLOB, blob, blob);
    XX(TYPE_TIMESTAMP, timestamp, datetime);

#undef XX
    return TYPE_NULL;
}

std::string Column::TypeToString(Type type) {
#define XX(a, b) \
    if(a == type) { \
        return #b; \
    }
    //#b 表示把传入的参数 b 直接变成一个字符串字面量。
    //不论 b 是变量、标识符、还是类型名，它都不会被求值或解析类型，而是被原样当作字符拷贝加上引号。

    XX(TYPE_INT8, int8_t);
    XX(TYPE_UINT8, uint8_t);
    XX(TYPE_INT16, int16_t);
    XX(TYPE_UINT16, uint16_t);
    XX(TYPE_INT32, int32_t);
    XX(TYPE_UINT32, uint32_t);
    XX(TYPE_FLOAT, float);
    XX(TYPE_INT64, int64_t);
    XX(TYPE_UINT64, uint64_t);
    XX(TYPE_DOUBLE, double);
    XX(TYPE_STRING, std::string);
    XX(TYPE_TEXT, std::string);
    XX(TYPE_BLOB, std::string);
    XX(TYPE_TIMESTAMP, int64_t);
#undef XX
    return "null";
}


std::string Column::getSQLite3TypeString() {
#define XX(a, b) \
    if(a == m_dtype) {\
        return #b; \
    }

    XX(TYPE_INT8, INTEGER);
    XX(TYPE_UINT8, INTEGER);
    XX(TYPE_INT16, INTEGER);
    XX(TYPE_UINT16, INTEGER);
    XX(TYPE_INT32, INTEGER);
    XX(TYPE_UINT32, INTEGER);
    XX(TYPE_FLOAT, REAL);
    XX(TYPE_INT64, INTEGER);
    XX(TYPE_UINT64, INTEGER);
    XX(TYPE_DOUBLE, REAL);
    XX(TYPE_STRING, TEXT);
    XX(TYPE_TEXT, TEXT);
    XX(TYPE_BLOB, BLOB);
    XX(TYPE_TIMESTAMP, TIMESTAMP);
#undef XX
    return "";
}

std::string Column::getBindString() {
#define XX(a, b) \
    if(a == m_dtype) { \
        return "bind" #b; \   // #b 把 Int32 转成字符串 "Int32"，然后与 "bind" 拼接成 "bindInt32"。
    }
    XX(TYPE_INT8, Int8);
    XX(TYPE_UINT8, Uint8);
    XX(TYPE_INT16, Int16);
    XX(TYPE_UINT16, Uint16);
    XX(TYPE_INT32, Int32);
    XX(TYPE_UINT32, Uint32);
    XX(TYPE_FLOAT, Float);
    XX(TYPE_INT64, Int64);
    XX(TYPE_UINT64, Uint64);
    XX(TYPE_DOUBLE, Double);
    XX(TYPE_STRING, String);
    XX(TYPE_TEXT, String);
    XX(TYPE_BLOB, Blob);
    XX(TYPE_TIMESTAMP, Time);
#undef XX
    return "";
}


std::string Column::getGetString() {
#define XX(a, b) \
    if(a == m_dtype) { \
        return "get" #b; \
    }
    XX(TYPE_INT8, Int8);
    XX(TYPE_UINT8, Uint8);
    XX(TYPE_INT16, Int16);
    XX(TYPE_UINT16, Uint16);
    XX(TYPE_INT32, Int32);
    XX(TYPE_UINT32, Uint32);
    XX(TYPE_FLOAT, Float);
    XX(TYPE_INT64, Int64);
    XX(TYPE_UINT64, Uint64);
    XX(TYPE_DOUBLE, Double);
    XX(TYPE_STRING, String);
    XX(TYPE_TEXT, String);
    XX(TYPE_BLOB, Blob);
    XX(TYPE_TIMESTAMP, Time);
#undef XX
    return "";
}

// 将字段的默认值（m_default）转换为对应的 C++ 字符串表达形式
std::string Column::getDefaultValueString() {
    // 情况 1：默认值为空，返回空字符串
    if(m_default.empty()) {
        return "";  // 表示没有默认值
    }
    // 情况 2：字段类型为数值类型（如 int、float、double 等）
    // TYPE_DOUBLE 通常是数值类型的最大枚举值，判断其及以下
    if(m_dtype <= TYPE_DOUBLE) {
        return m_default;  // 原样返回数值默认值，比如 "123" 或 "3.14"
    }
    // 情况 3：字段类型为字符串、文本或二进制类型（如 string、text、blob）
    // 需要加上双引号变成合法的 C++ 字符串字面量
    if(m_dtype <= TYPE_BLOB) {
        return "\"" + m_default + "\"";  // 例如返回 "\"hello\""
    }
    // 情况 4：默认值为 "current_timestamp"，表示当前时间
    // 转换为 C++ 中获取当前时间的函数 time(0)
    if(m_default == "current_timestamp") {
        return "time(0)";  // 表达当前 Unix 时间戳
    }
    // 情况 5：其余情况（通常是时间字符串，如 "2024-01-01 12:00:00"）
    // 转换为时间戳的函数调用形式
    return "sylar::Str2Time(\"" + m_default + "\")";  // 返回类似 sylar::Str2Time("2024-01-01 12:00:00")
}

// 获取字段在 SQLite3 中的默认值表达（返回 SQL 字符串的一部分）
std::string Column::getSQLite3Default() {
    // 情况 1：字段类型是整数/浮点等数值类型（TYPE_UINT64 及以下）
    if(m_dtype <= TYPE_UINT64) {
        // 默认值为空，返回数字类型默认值 0
        if(m_default.empty()) {
            return "0";
        }
        // 否则直接返回默认值（比如 123、3.14 等）
        return m_default;
    }
    // 情况 2：字段类型是 BLOB、TEXT、STRING 等（<= TYPE_BLOB）
    if(m_dtype <= TYPE_BLOB) {
        // 默认值为空，返回空字符串 SQL 表达式
        if(m_default.empty()) {
            return "''";  // SQLite 中空字符串的写法
        }
        // 否则加上单引号，作为 SQL 中的字符串字面量
        return "'" + m_default + "'";
    }
    // 情况 3：字段类型是时间类（如 TIMESTAMP），且没有默认值
    if(m_default.empty()) {
        // 使用一个通用的早期时间戳作为默认时间
        return "'1980-01-01 00:00:00'";
    }
    // 情况 4：其余情况，直接返回默认值（比如 "CURRENT_TIMESTAMP"）
    return m_default;
}

std::string Column::getSetFunImpl(const std::string& class_name, int idx) const {
    std::stringstream ss;
    ss << "void " << GetAsClassName(class_name) << "::" << GetAsSetFunName(m_name) << "(const "
       << TypeToString(m_dtype) << "& v) {" << std::endl;
    ss << "    " << GetAsMemberName(m_name) << " = v;" << std::endl;
    ss << "}" << std::endl;
    return ss.str();
}

std::string Column::getMemberDefine() const {
    std::stringstream ss;
    ss << TypeToString(m_dtype) << " " << GetAsMemberName(m_name) << ";" << std::endl;
    return ss.str();
}

std::string Column::getGetFunDefine() const {
    std::stringstream ss;
    ss << "const " << TypeToString(m_dtype) << "& " << GetAsGetFunName(m_name)
       << "() { return " << GetAsMemberName(m_name) << "; }" << std::endl;
    return ss.str();
}

std::string Column::getSetFunDefine() const {
    std::stringstream ss;
    ss << "void " << GetAsSetFunName(m_name) << "(const "
       << TypeToString(m_dtype) << "& v);" << std::endl;
    return ss.str();
}

// <column 
//     name="age" 
//     type="int32_t" 
//     desc="用户年龄" 
//     default="18" 
//     update="now()" 
//     length="4" 
//     auto_increment="false"
// />
bool Column::init(const tinyxml2::XMLElement& node) {
    if(!node.Attribute("name")) {
        SYLAR_LOG_ERROR(g_logger) << "column name not exists";
        return false;
    }
    m_name = node.Attribute("name");

    if(!node.Attribute("type")) {
        SYLAR_LOG_ERROR(g_logger) << "column name=" << m_name
            << " type is null";
        return false;
    }
    m_type = node.Attribute("type");
    m_dtype = ParseType(m_type);
    if(m_dtype == TYPE_NULL) {
        SYLAR_LOG_ERROR(g_logger) << "column name=" << m_name
            << " type=" << m_type
            << " type is invalid";
        return false;
    }
    if(node.Attribute("desc")) {
        m_desc = node.Attribute("desc");
    }

    if(node.Attribute("default")) {
        m_default = node.Attribute("default");
    }

    if(node.Attribute("update")) {
        m_update = node.Attribute("update");
    }

    if(node.Attribute("length")) {
        m_length = node.IntAttribute("length");
    } else {
        m_length = 0;
    }

    m_autoIncrement = node.BoolAttribute("auto_increment", false);
    return true;
}

}
}