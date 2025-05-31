#ifndef __SYLAR_ORM_TABLE_H__
#define __SYLAR_ORM_TABLE_H__

#include "column.h"   
#include "index.h"   
#include <fstream> 

namespace sylar {
namespace orm {

/**
 * @brief ORM 表结构类，用于描述数据库中的一张表结构，包括列、索引、命名空间等信息，
 *        并支持根据定义生成 C++ 映射类代码（如 DAO 类、模型类等）。
 */
class Table {
public:
    typedef std::shared_ptr<Table> ptr;  ///< 智能指针类型定义

    /// 获取表名
    const std::string& getName() const { return m_name;}

    /// 获取表所属命名空间
    const std::string& getNamespace() const { return m_namespace;}

    /// 获取表的描述信息（可用于注释或文档）
    const std::string& getDesc() const { return m_desc;}

    /// 获取所有列定义
    const std::vector<Column::ptr>& getCols() const { return m_cols;}

    /// 获取所有索引定义
    const std::vector<Index::ptr>& getIdxs() const { return  m_idxs;}

    /**
     * @brief 从 XML 节点初始化表结构
     * @param node XML 中定义 <table> 元素
     * @return 成功返回 true，否则 false
     */
    bool init(const tinyxml2::XMLElement& node);

    /**
     * @brief 根据当前表结构定义生成 C++ 映射代码（包括模型类、SQL 构造函数等）
     * @param path 代码生成输出路径
     */
    void gen(const std::string& path);

    /// 获取根据表名生成的文件名（用于代码生成输出文件）
    std::string getFilename() const;

private:
    /**
     * @brief 生成 `.h` 头文件
     */
    void gen_inc(const std::string& path);

    /**
     * @brief 生成 `.cpp` 实现文件
     */
    void gen_src(const std::string& path);

    /**
     * @brief 生成模型类 `toString()` 方法头文件部分（声明）
     */
    std::string genToStringInc();

    /**
     * @brief 生成模型类 `toString()` 方法实现部分（定义）
     */
    std::string genToStringSrc(const std::string& class_name);

    /**
     * @brief 生成插入语句构造代码
     */
    std::string genToInsertSQL(const std::string& class_name);

    /**
     * @brief 生成更新语句构造代码
     */
    std::string genToUpdateSQL(const std::string& class_name);

    /**
     * @brief 生成删除语句构造代码
     */
    std::string genToDeleteSQL(const std::string& class_name);

    /**
     * @brief 获取主键列列表（可用于构造 WHERE 子句或识别唯一性）
     */
    std::vector<Column::ptr> getPKs() const;

    /**
     * @brief 获取指定名称的列定义
     * @param name 列名
     */
    Column::ptr getCol(const std::string& name) const;

    /**
     * @brief 根据主键信息生成 WHERE 子句代码
     */
    std::string genWhere() const;

    /**
     * @brief 生成 DAO 类的头文件代码（声明）
     */
    void gen_dao_inc(std::ofstream& ofs);

    /**
     * @brief 生成 DAO 类的源文件代码（定义）
     */
    void gen_dao_src(std::ofstream& ofs);

    /**
     * @brief 支持的数据库类型（目前支持 SQLite3 和 MySQL）
     */
    enum DBType {
        TYPE_SQLITE3 = 1,
        TYPE_MYSQL = 2
    };

private:
    std::string m_name;            ///< 表名
    std::string m_namespace;       ///< 表映射的命名空间
    std::string m_desc;            ///< 表描述信息
    std::string m_subfix = "_info";///< 自动生成表名(类名)的后缀（如 User → User_info）

    DBType m_type = TYPE_SQLITE3;  ///< 当前使用的数据库类型，默认是 SQLite3

    std::string m_dbclass = "sylar::IDB";       ///< 用于执行 SQL 操作的数据库接口类名
    std::string m_queryclass = "sylar::IDB";    ///< 查询类接口（可定制扩展）
    std::string m_updateclass = "sylar::IDB";   ///< 更新类接口（可定制扩展）

    std::vector<Column::ptr> m_cols;  ///< 列定义列表
    std::vector<Index::ptr> m_idxs;   ///< 索引定义列表
};

} // namespace orm
} // namespace sylar

#endif // __SYLAR_ORM_TABLE_H__
