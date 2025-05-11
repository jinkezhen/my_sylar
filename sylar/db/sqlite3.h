/**
 * @file sqlite3.h
 * @brief SQLite3封装
 * @date 2025-05-09
 * @copyright Copyright (c) 2025年 All rights reserved
 */

#ifndef __SYLAR_DB_SQLITE3_H__
#define __SYLAR_DB_SQLITE3_H__

#include <sqlite3.h>
#include <memory>
#include <string>
#include <list>
#include <map>
#include "sylar/noncopyable.h"
#include "db.h"
#include "sylar/mutex.h"
#include "sylar/singleton.h"

namespace {
class SQLite3Binder;

namespace {
template<size_t N, typename... Args>
struct SQLite3Binder {
    static int Bind(std::shared_ptr<SQLite3Stmt> stmt) { return SQLITE_OK; }
}; 
}

class SQLite3Manager;
class SQLite3 : public IDB
              , public std::enable_shared_from_this<SQLite3> {
friend class SQLite3Manager;
public:
    // 这个 enum Flags 枚举定义了 SQLite 的数据库打开模式的简化封装：
    enum Flags {
        READONLY = SQLITE_OPEN_READONLY,
        READWRITE = SQLITE_OPEN_READWRITE,
        CREATE = SQLITE_OPEN_CREATE
    };

    typedef std::shared_ptr<SQLite3> ptr;
    static SQLite3::ptr Create(sqlite3* db);
    static SQLite3::ptr Create(const std::string& dbname
            ,int flags = READWRITE | CREATE);
    ~SQLite3();


    IStmt::ptr prepare(const std::string& stmt) override;

    int getErrno() override;
    std::string getErrStr() override;

    int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query(const char* format, ...) override;
    ISQLData::ptr query(const std::string& sql) override;

    ITransaction::ptr openTransaction(bool auto_commit = false) override;

    template<typename... Args>
    int execStmt(const char* stmt, Args&&... args);

    template<class... Args>
    ISQLData::ptr queryStmt(const char* stmt, const Args&... args);

    int close();

    sqlite3* getDB() const { return m_db;}
private:
    SQLite3(sqlite3* db);
private:
    sqlite3* m_db;
    uint64_t m_lastUsedTime = 0;
};


class SQLite3Stmt;
class SQLite3Data : public ISQLData {
public:
    typedef std::shared_ptr<SQLite3Data> ptr;
    SQLite3Data(std::shared_ptr<SQLite3Stmt> stmt, int err
                ,const char* errstr);
    int getErrno() const override { return m_errno;}
    const std::string& getErrStr() const override { return m_errstr;}

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx);
    int getColumnType(int idx);

    std::string getColumnName(int idx);

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;

    // 类似于游标移动，尝试读取下一行数据。
    bool next();
private:
    int m_errno;
    //记录是否是第一次调用 next()，用于控制 sqlite3_step 的逻辑。
    bool m_first;
    std::string m_errstr;
    //SQL 语句对象，持有当前结果集的实际数据。
    std::shared_ptr<SQLite3Stmt> m_stmt;
};


class SQLite3Stmt : public IStmt
                    ,public std::enable_shared_from_this<SQLite3Stmt> {
friend class SQLite3Data;
public:
    typedef std::shared_ptr<SQLite3Stmt> ptr;
    //这个枚举 SQLite3Stmt::Type 的作用，是指定绑定文本或二进制数据（如 string 或 blob）时，
    //SQLite 对数据内存的管理策略。它决定了绑定数据时传入的指针是：
    // 需要 SQLite 自己复制一份（COPY）
    // 还是直接引用你传入的数据（REF）
    enum Type {
        COPY = 1,
        REF = 2
    };
    static SQLite3Stmt::ptr Create(SQLite3::ptr db, const char* stmt);

    int prepare(const char* stmt);
    ~SQLite3Stmt();
    int finish();

    int bind(int idx, int32_t value);
    int bind(int idx, uint32_t value);
    int bind(int idx, double value);
    int bind(int idx, int64_t value);
    int bind(int idx, uint64_t value);
    int bind(int idx, const char* value, Type type = COPY);
    int bind(int idx, const void* value, int len, Type type = COPY);
    int bind(int idx, const std::string& value, Type type = COPY);
    // for null type
    int bind(int idx);

    int bindInt8(int idx, const int8_t& value) override;
    int bindUint8(int idx, const uint8_t& value) override;
    int bindInt16(int idx, const int16_t& value) override;
    int bindUint16(int idx, const uint16_t& value) override;
    int bindInt32(int idx, const int32_t& value) override;
    int bindUint32(int idx, const uint32_t& value) override;
    int bindInt64(int idx, const int64_t& value) override;
    int bindUint64(int idx, const uint64_t& value) override;
    int bindFloat(int idx, const float& value) override;
    int bindDouble(int idx, const double& value) override;
    int bindString(int idx, const char* value) override;
    int bindString(int idx, const std::string& value) override;
    int bindBlob(int idx, const void* value, int64_t size) override;
    int bindBlob(int idx, const std::string& value) override;
    int bindTime(int idx, const time_t& value) override;
    int bindNull(int idx) override;

    int bind(const char* name, int32_t value);
    int bind(const char* name, uint32_t value);
    int bind(const char* name, double value);
    int bind(const char* name, int64_t value);
    int bind(const char* name, uint64_t value);
    int bind(const char* name, const char* value, Type type = COPY);
    int bind(const char* name, const void* value, int len, Type type = COPY);
    int bind(const char* name, const std::string& value, Type type = COPY);
    // for null type
    int bind(const char* name);

    int step();
    int reset();

    ISQLData::ptr query() override;
    int execute() override;
    int64_t getLastInsertId() override;

    int getErrno() override;
    std::string getErrStr() override;
protected:
    SQLite3Stmt(SQLite3::ptr db);
protected:
    SQLite3::ptr m_db;
    sqlite3_stmt* m_stmt;
};

class SQLite3Transaction : public ITransaction {
public:
    //这个枚举 SQLite3Transaction::Type 的作用是：
    //指定 SQLite 事务的启动模式（事务隔离级别的行为控制），
    //它决定了在开始事务（BEGIN）时，数据库如何获得锁，从而影响并发访问行为。
    // 1. DEFERRED
    // 默认模式，等到实际执行 SQL 时才决定是否获取锁。
    // 不立即加锁，只在需要时才获取读锁或写锁。
    // 适合读多写少、并发友好的场景。
    // 对应 SQL：BEGIN DEFERRED;
    // 2. IMMEDIATE
    // 启动事务时立即获取写锁（或者至少试图获取它）。
    // 保证事务内可以写，但其他连接无法写，能读。
    // 可以避免后续写入时才发生锁冲突。
    // 对应 SQL：BEGIN IMMEDIATE;
    // 3. EXCLUSIVE
    // 启动事务时立即获取独占锁，阻止其他任何读写操作。
    // 最安全也最强的隔离，适用于关键写事务，但性能最低。
    // 对应 SQL：BEGIN EXCLUSIVE;
    enum Type {
        DEFERRED = 0,
        IMMEDIATE = 1,
        EXCLUSIVE = 2
    };
    SQLite3Transaction(SQLite3::ptr db
                        ,bool auto_commit = false 
                        ,Type type = DEFERRED);
    ~SQLite3Transaction();
    bool begin() override;
    bool commit() override;
    bool rollback() override;

    int execute(const char* format, ...) override;
    int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
private:
    SQLite3::ptr m_db;
    Type m_type;
    // 状态值	 含义说明
    // 0	  事务尚未开始
    // 1	  事务已开始，尚未提交或回滚
    // 2	  事务已提交
    // 3	  事务已回滚
    int8_t m_status;
    bool m_autoCommit;
};

class SQLite3Manager {
public:
    typedef sylar::Mutex MutexType;
    SQLite3Manager();
    ~SQLite3Manager();

    SQLite3::ptr get(const std::string& name);
    void registerSQLite3(const std::string& name, const std::map<std::string, std::string>& params);

    void checkConnection(int sec = 30);

    uint32_t getMaxConn() const { return m_maxConn;}
    void setMaxConn(uint32_t v) { m_maxConn = v;}

    int execute(const std::string& name, const char* format, ...);
    int execute(const std::string& name, const char* format, va_list ap);
    int execute(const std::string& name, const std::string& sql);

    ISQLData::ptr query(const std::string& name, const char* format, ...);
    ISQLData::ptr query(const std::string& name, const char* format, va_list ap); 
    ISQLData::ptr query(const std::string& name, const std::string& sql);

    SQLite3Transaction::ptr openTransaction(const std::string& name, bool auto_commit);
private:
    void freeSQLite3(const std::string& name, SQLite3* m);
private:
    uint32_t m_maxConn;
    MutexType m_mutex;
    std::map<std::string, std::list<SQLite3*> > m_conns;
    std::map<std::string, std::map<std::string, std::string> > m_dbDefines;
};

typedef sylar::Singleton<SQLite3Manager> SQLite3Mgr;

namespace {
template<typename... Args>
int bindX(SQLite3Stmt::ptr stmt, const Args&... args) {
    return SQLite3Binder<1, Args...>::Bind(stmt, args...);
}
}

template<typename... Args>
int SQLite3::execStmt(const char* stmt, Args&&... args) {
    auto st = SQLite3Stmt::Create(shared_from_this(), stmt);
    if(!st) {
        return -1;
    }
    int rt = bindX(st, args...);
    if(rt != SQLITE_OK) {
        return rt;
    }
    return st->execute();
}

template<class... Args>
ISQLData::ptr SQLite3::queryStmt(const char* stmt, const Args&... args) {
    auto st = SQLite3Stmt::Create(shared_from_this(), stmt);
    if(!st) {
        return nullptr;
    }
    int rt = bindX(st, args...);
    if(rt != SQLITE_OK) {
        return nullptr;
    }
    return st->query();
}

namespace {

template<size_t N, typename Head, typename... Tail>
struct SQLite3Binder<N, Head, Tail...> {
    static int Bind(SQLite3Stmt::ptr stmt
                    ,const Head&, const Tail&...) {
        static_assert(sizeof...(Tail) < 0, "invalid type");
        return SQLITE_OK;
    }
};

#define XX(type, type2) \
template<size_t N, typename... Tail> \
struct SQLite3Binder<N, type, Tail...> { \
    static int Bind(SQLite3Stmt::ptr stmt \
                    , const type2& value \
                    , const Tail&... tail) { \
        int rt = stmt->bind(N, value); \
        if(rt != SQLITE_OK) { \
            return rt; \
        } \
        return SQLite3Binder<N + 1, Tail...>::Bind(stmt, tail...); \
    } \
};

XX(char*, char* const);
XX(const char*, char* const);
XX(std::string, std::string);
XX(int8_t, int32_t);
XX(uint8_t, int32_t);
XX(int16_t, int32_t);
XX(uint16_t, int32_t);
XX(int32_t, int32_t);
XX(uint32_t, int32_t);
XX(int64_t, int64_t);
XX(uint64_t, int64_t);
XX(float, double);
XX(double, double);
#undef XX

}
}
#endif