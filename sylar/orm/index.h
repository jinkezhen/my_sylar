#ifndef __SYLAR_ORM_INDEX_H__
#define __SYLAR_ORM_INDEX_H__

#include <memory>
#include <string>
#include <vector>
#include "tinyxml2.h"

namespace sylar {
namespace orm {

/**
 * @brief 数据库索引信息的描述类
 *        用于 ORM 映射中表示表结构中的索引（主键、唯一索引、普通索引等）
 */
class Index {
public:
    /**
     * @brief 索引类型枚举
     */
    enum Type {
        TYPE_NULL = 0,  // 空类型，默认值
        TYPE_PK,        // 主键 Primary Key
        TYPE_UNIQ,      // 唯一索引 Unique
        TYPE_INDEX      // 普通索引 Index
    };

    /// 智能指针定义
    typedef std::shared_ptr<Index> ptr;

    /**
     * @brief 获取索引名称
     * @return 索引名称
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 获取索引类型字符串（如 "pk", "uniq", "index"）
     * @return 索引类型字符串
     */
    const std::string& getType() const { return m_type; }

    /**
     * @brief 获取索引的描述信息（可选）
     * @return 描述字符串
     */
    const std::string& getDesc() const { return m_desc; }

    /**
     * @brief 获取索引所包含的字段名列表
     * @return 索引字段名数组
     */
    const std::vector<std::string>& getCols() const { return m_cols; }

    /**
     * @brief 获取枚举形式的索引类型
     * @return Type 枚举值
     */
    Type getDType() const { return m_dtype; }

    /**
     * @brief 从 XML 节点中初始化索引信息（通常用于从 XML schema 加载结构）
     * @param node XML 元素节点
     * @return 是否初始化成功
     */
    bool init(const tinyxml2::XMLElement& node);

    /**
     * @brief 判断是否为主键索引（type == "pk"）
     * @return 是否为主键
     */
    bool isPK() const { return m_type == "pk"; }

    /**
     * @brief 将字符串类型转为枚举类型
     * @param v 类型字符串（如 "pk", "uniq"）
     * @return 枚举值 Type
     */
    static Type ParseType(const std::string& v);

    /**
     * @brief 将枚举类型转为字符串
     * @param v 枚举值 Type
     * @return 类型字符串
     */
    static std::string TypeToString(Type v);

private:
    // CREATE UNIQUE INDEX idx_email ON user(email);
    //其中 idx_email 就是索引名，对应的就是 m_name = "idx_email"。
    std::string m_name;                  ///< 索引名称
    std::string m_type;                  ///< 索引类型字符串（如 "pk", "uniq", "index"）
    std::string m_desc;                  ///< 索引描述（可选）
    //联合唯一索引 CREATE UNIQUE INDEX uniq_name_age ON user(name, age);
    //则：m_cols = {"name", "age"}，即联合索引 m_cols 是很关键的数据，决定索引优化器在执行 SQL 时用哪些字段进行加速。
    std::vector<std::string> m_cols;     ///< 索引作用的列名列表

    Type m_dtype = TYPE_NULL;            ///< 枚举类型（与 m_type 对应，用于逻辑判断）
};

} // namespace orm
} // namespace sylar

#endif // __SYLAR_ORM_INDEX_H__
