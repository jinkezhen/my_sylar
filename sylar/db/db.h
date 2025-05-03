/**
 * @file db.h
 * @brief 数据库接口定义头文件，提供统一的 SQL 数据访问抽象接口
 * @author ...
 * @date ...
 *
 * 本文件定义了 Sylar 框架中数据库相关的抽象接口类，包括 SQL 查询、更新、数据访问、
 * 预处理语句、事务管理等，旨在对底层数据库实现（如 SQLite、MySQL、Redis）进行统一封装，
 * 提供一致的编程接口，便于上层业务逻辑与数据库解耦。
 */

 #ifndef __SYLAR_DB_DB_H__
 #define __SYLAR_DB_DB_H__
 
 #include <memory>
 #include <string>
 
 namespace sylar {
 
 /**
  * @brief SQL 查询结果的数据访问接口
  */
 class ISQLData {
 public:
     typedef std::shared_ptr<ISQLData> ptr;
     virtual ~ISQLData() {}
 
     // 错误码与错误信息
     virtual int getErrno() const = 0;
     virtual const std::string& getErrStr() const = 0;
 
     // 行列信息
     virtual int getDataCount() = 0;               // 数据行数
     virtual int getColumnCount() = 0;             // 列数
     virtual int getColumnBytes(int idx) = 0;      // 第 idx 列数据的字节数
     virtual int getColumnType(int idx) = 0;       // 第 idx 列数据类型
     virtual std::string getColumnName(int idx) = 0; // 第 idx 列名
 
     // 获取第 idx 列数据（各类型）
     virtual bool isNull(int idx) = 0;
     virtual int8_t getInt8(int idx) = 0;
     virtual uint8_t getUint8(int idx) = 0;
     virtual int16_t getInt16(int idx) = 0;
     virtual uint16_t getUint16(int idx) = 0;
     virtual int32_t getInt32(int idx) = 0;
     virtual uint32_t getUint32(int idx) = 0;
     virtual int64_t getInt64(int idx) = 0;
     virtual uint64_t getUint64(int idx) = 0;
     virtual float getFloat(int idx) = 0;
     virtual double getDouble(int idx) = 0;
     virtual std::string getString(int idx) = 0;
     virtual std::string getBlob(int idx) = 0;
     virtual time_t getTime(int idx) = 0;
 
     // 移动到下一行
     virtual bool next() = 0;
 };
 
 /**
  * @brief SQL 更新操作接口（如 INSERT、UPDATE、DELETE）
  */
 class ISQLUpdate {
 public:
     virtual ~ISQLUpdate() {}
 
     // 执行 SQL 更新语句，支持格式化或直接传入字符串
     virtual int execute(const char* format, ...) = 0;
     virtual int execute(const std::string& sql) = 0;
 
     // 获取上一次 INSERT 操作的自增 ID
     virtual int64_t getLastInsertId() = 0;
 };
 
 /**
  * @brief SQL 查询操作接口（如 SELECT）
  */
 class ISQLQuery {
 public:
     virtual ~ISQLQuery() {}
 
     // 执行 SQL 查询语句，返回 ISQLData 对象
     virtual ISQLData::ptr query(const char* format, ...) = 0;
     virtual ISQLData::ptr query(const std::string& sql) = 0;
 };
 
 /**
  * @brief SQL 预处理语句接口
  */
 class IStmt {
 public:
     typedef std::shared_ptr<IStmt> ptr;
     virtual ~IStmt(){}
 
     // 绑定参数（按索引）
     virtual int bindInt8(int idx, const int8_t& value) = 0;
     virtual int bindUint8(int idx, const uint8_t& value) = 0;
     virtual int bindInt16(int idx, const int16_t& value) = 0;
     virtual int bindUint16(int idx, const uint16_t& value) = 0;
     virtual int bindInt32(int idx, const int32_t& value) = 0;
     virtual int bindUint32(int idx, const uint32_t& value) = 0;
     virtual int bindInt64(int idx, const int64_t& value) = 0;
     virtual int bindUint64(int idx, const uint64_t& value) = 0;
     virtual int bindFloat(int idx, const float& value) = 0;
     virtual int bindDouble(int idx, const double& value) = 0;
     virtual int bindString(int idx, const char* value) = 0;
     virtual int bindString(int idx, const std::string& value) = 0;
     virtual int bindBlob(int idx, const void* value, int64_t size) = 0;
     virtual int bindBlob(int idx, const std::string& value) = 0;
     virtual int bindTime(int idx, const time_t& value) = 0;
     virtual int bindNull(int idx) = 0;
 
     // 执行预处理语句或执行查询
     virtual int execute() = 0;
     virtual int64_t getLastInsertId() = 0;
     virtual ISQLData::ptr query() = 0;
 
     // 错误信息
     virtual int getErrno() = 0;
     virtual std::string getErrStr() = 0;
 };
 
 /**
  * @brief 数据库事务接口
  */
 class ITransaction : public ISQLUpdate {
 public:
     typedef std::shared_ptr<ITransaction> ptr;
     virtual ~ITransaction() {};
 
     virtual bool begin() = 0;      // 开始事务
     virtual bool commit() = 0;     // 提交事务
     virtual bool rollback() = 0;   // 回滚事务
 };
 
 /**
  * @brief 数据库操作统一接口，继承查询与更新功能
  */
 class IDB : public ISQLUpdate, public ISQLQuery {
 public:
     typedef std::shared_ptr<IDB> ptr;
     virtual ~IDB() {}
 
     // 预处理语句接口
     virtual IStmt::ptr prepare(const std::string& stmt) = 0;
 
     // 错误信息
     virtual int getErrno() = 0;
     virtual std::string getErrStr() = 0;
 
     // 打开事务（支持设置是否自动提交）
     virtual ITransaction::ptr openTransaction(bool auto_commit = false) = 0;
 };
 
 }
 
 #endif // __SYLAR_DB_DB_H__
 