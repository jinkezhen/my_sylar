#ifndef __SYLAR_ORM_COLUMN_H__
#define __SYLAR_ORM_COLUMN_H__

#include <memory>
#include <string>
#include "tinyxml2.h" // 用于解析 ORM 映射 XML 文件，生成 Column 实例配置

namespace sylar {
namespace orm {

// 前向声明：每个 Column 实际属于某个 Table
class Table;

/**
 * @brief 表字段描述类，用于 ORM 映射
 * 每个 Column 对象对应数据库表中的一列字段，
 * 支持字段名称、类型、默认值、自增标记、描述等信息。
 * 同时提供用于代码生成的接口（生成成员变量定义、getter/setter 等）。
 */
class Column {
friend class Table; // Table 可以访问 Column 的私有成员
public:
    typedef std::shared_ptr<Column> ptr;

    /**
     * @brief 枚举类型：支持的字段类型
     */
    enum Type {
        TYPE_NULL = 0,   // 未定义
        TYPE_INT8,
        TYPE_UINT8,
        TYPE_INT16,
        TYPE_UINT16,
        TYPE_INT32,
        TYPE_UINT32,
        TYPE_FLOAT,
        TYPE_DOUBLE,
        TYPE_INT64,
        TYPE_UINT64,
        TYPE_STRING,     // varchar
        TYPE_TEXT,       // text
        TYPE_BLOB,       // blob
        TYPE_TIMESTAMP   // 时间戳
    };

    // ---------- 字段信息访问接口（用于模板生成或外部读取） ----------
    /// 获取字段名（如 name / id）
    const std::string& getName() const { return m_name; }

    /// 获取字段类型（string 表示的原始类型，如 "int32" / "string"）
    const std::string& getType() const { return m_type; }

    /// 获取字段注释描述
    const std::string& getDesc() const { return m_desc; }

    /// 获取默认值的原始字符串
    const std::string& getDefault() const { return m_default; }

    /// 获取默认值在 C++ 中的写法，如 `"0"` -> `0`，或者 `"\"abc\""` -> `"abc"`
    std::string getDefaultValueString();

    /// 获取 SQLite3 中的默认值表达式字符串
    std::string getSQLite3Default();

    /// 判断是否自增字段（适用于主键）
    bool isAutoIncrement() const { return m_autoIncrement; }

    /// 获取字段的内部枚举类型
    Type getDType() const { return m_dtype; }

    /// 初始化字段信息，从 XML 节点中解析
    /// @param node XML 中 `<column>` 节点，包含 name/type/default 等属性
    /// @return 是否成功解析
    bool init(const tinyxml2::XMLElement& node);

    // ---------- 以下函数用于生成 ORM 对应类的代码段 ----------
    /// 生成成员变量定义，如：`int32_t m_id;`
    std::string getMemberDefine() const;

    /// 生成 getter 函数声明，如：`int32_t getId() const;`
    std::string getGetFunDefine() const;

    /// 生成 setter 函数声明，如：`void setId(int32_t v);`
    std::string getSetFunDefine() const;

    /**
     * 生成 setter 函数实现，如：
     * void UserInfo::setId(int32_t v) { m_id = v; }
     *
     * @param class_name 类名，如 UserInfo
     * @param idx 字段索引，可能用于调试或设置更新标记
     */
    std::string getSetFunImpl(const std::string& class_name, int idx) const;

    /// 获取字段索引（在表中字段的顺序位置）
    int getIndex() const { return m_index; }

    // ---------- 工具函数 ----------
    /// 将字符串类型解析为内部枚举 Type
    static Type ParseType(const std::string& v);

    /// 将 Type 枚举转换为字符串表示，如 TYPE_INT32 -> "int32"
    static std::string TypeToString(Type type);

    /// 获取当前字段类型的字符串描述（如 "int32"）
    std::string getDTypeString() { return TypeToString(m_dtype); }

    /// 获取该字段在 SQLite 中的类型表示，如 "INTEGER"
    std::string getSQLite3TypeString();

    /// 获取该字段在 MySQL 中的类型表示，如 "INT(11)"
    std::string getMySQLTypeString();

    /// 生成绑定字段时的代码片段，如用于 SQLite3 的绑定语句（bind 用）
    std::string getBindString();

    /// 生成字段访问语句（用于 DAO 查询中赋值等）
    std::string getGetString();

    /// 获取字段更新表达式（当存在 update= 属性时）
    const std::string& getUpdate() const { return m_update; }

private:
    // ---------- 字段元数据 ----------
    std::string m_name;       ///< 字段名，如 "id"
    std::string m_type;       ///< 字段类型字符串，如 "int32"
    std::string m_default;    ///< 默认值字符串，如 "0"、"\"default name\""
    std::string m_update;     ///< 可选，更新表达式（MySQL 专用，如 "CURRENT_TIMESTAMP"）
    std::string m_desc;       ///< 字段说明
    int m_index;              ///< 字段在表中的顺序索引

    bool m_autoIncrement;     ///< 是否为自增字段
    Type m_dtype;             ///< 字段内部类型（枚举表示）
    int m_length;             ///< 字段长度（对 VARCHAR 有意义）
};

} // namespace orm
} // namespace sylar

#endif
