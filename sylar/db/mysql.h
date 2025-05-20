#ifndef __SYLAR_DB_MYSQL_H__
#define __SYLAR_DB_MYSQL_H__

#include <mysql/mysql.h>
#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <list>
#include "sylar/mutex.h"
#include "db.h"
#include "sylar/singleton.h"

namespace sylar {

// MySQLTime 是一个简单的 时间包装结构体。
// 用于封装 time_t 类型（即 Unix 时间戳），方便作为参数传给数据库或从数据库中读取时间。
// 提供构造函数支持简洁初始化，比如：
//MySQLTime now(time(nullptr));  封装当前时间
struct MySQLTime {
    MySQLTime(time_t t) : ts(t) {}
    time_t ts;
};

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts);
bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt);

// MySQLRes 是 Sylar 框架中用于 封装 MySQL 查询结果集 MYSQL_RES 的类，
// 提供统一、安全、类型明确的访问接口，隐藏底层原始 C API 操作细节，并
// 通过继承 ISQLData 接口，使它能与其他数据库查询结果统一处理。
// 假设你执行了一个 MySQL 查询：SELECT id, name, age, birthday FROM users;
// MySQLRes 就是对这类结果的一种封装，你可以用这些函数从每一行中按下标读取字段值。
// 用于简单的、一次性的 SQL 查询
class MySQLRes : public ISQLData {
public:
    typedef std::shared_ptr<MySQLRes> ptr;
    // row	MYSQL_ROW	当前查询结果中的一行，类型为 char**，每个字段是 char*（字符串形式）
    // field_count	int	当前行中字段的总数量（即列数）
    // row_no	int	当前是第几行，从 0 开始计数
    typedef std::function<bool(MYSQL_ROW row, int field_count, int row_no)> data_cb;

    // res	MYSQL_RES*	MySQL 原生查询结果指针，即 mysql_store_result() 的返回值。表示完整的查询结果集。
    // eno	int	错误码，通常是 mysql_errno(conn) 的返回结果。如果没有错误，一般为 0。
    // estr	const char*	错误信息字符串，通常是 mysql_error(conn) 的返回值。描述出错原因。
    MySQLRes(MYSQL_RES* res, int eno, const char* estr);
    MYSQL_RES* get() const { return m_data.get();}
    int getErrno() const { return m_errno; }
    const std::string& getErrStr() const { return m_errstr; }
    // 用于遍历当前 MySQLRes 对象中存储的查询结果的每一行，并对每一行数据执行用户定义的回调函数。
    bool foreach(data_cb cb);

    // 获取当前结果集的总行数（注意：通常是一次性全部提取结果时才有效）
    int getDataCount() override;
    // 获取当前结果集的总列数（即字段数量）
    int getColumnCount() override;
    // 获取第 idx 列的数据长度（字节数），适用于字符串、BLOB 等变长类型
    int getColumnBytes(int idx) override;
    // 获取第 idx 列的字段类型（返回的是 MySQL 的字段类型常量，如 MYSQL_TYPE_LONG）
    int getColumnType(int idx) override;
    // 获取第 idx 列的字段名称（列名），用于打印或结构分析
    std::string getColumnName(int idx) override;

    // 判断当前行第 idx 列是否为 NULL
    bool isNull(int idx) override;
    // 获取第 idx 列的值并转换为 int8_t 类型（适用于 tinyint 类型字段）
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
    // 获取第 idx 列的时间字段（如 DATETIME、TIMESTAMP），转换为 time_t 类型
    time_t getTime(int idx) override;
    // 移动到结果集中的下一行，成功返回 true，否则返回 false
    bool next() override;

private:
    //mysql错误码，一般是mysql_errno(conn)的返回值
    int m_errno;
    //mysql错误描述字符串，一般是mysql_error(conn)的返回值
    std::string m_errstr;
    //当前正在处理的行数据
    //这里的 m_cur 是 MYSQL_ROW 类型，实际上是 char**，每个 m_cur[idx] 是一个指向以 null 结尾的 字符串 的 char*，即：
    //无论这个字段在数据库中是整数、浮点还是字符串，MySQL 都会把它转换为字符串形式返回。
    MYSQL_ROW m_cur;
    //是与m_cur对应的数组，保存了当前行每列数据的字节长度
    unsigned long* m_curLength;
    //查询结果集对象，由mysql_store_result()返回
    std::shared_ptr<MYSQL_RES> m_data;
};

class MySQLStmt;

class MySQLStmtRes : public ISQLData {
friend class MySQLStmt;
public:
    typedef std::shared_ptr<MySQLStmtRes> ptr;
    // 传入一个已准备好的 MySQL 预处理语句对象（MySQLStmt），用于关联结果集。结果集的数据来源依赖于这个预处理语句的执行结果。
    static MySQLStmtRes::ptr Create(std::shared_ptr<MySQLStmt> stmt);
    ~MySQLStmtRes();

    int getErrno() const { return m_errno;}
    const std::string& getErrStr() const { return m_errstr;}

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

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
    bool next() override;
    
private:
    MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno, const std::string& estr);
    // 封装用于存储一列 MySQL 查询结果的结构体
    struct Data {
        Data();     // 构造函数，初始化成员变量
        ~Data();    // 析构函数，释放分配的数据内存

        // 为当前列的数据分配指定大小的内存
        void alloc(size_t size);

        bool is_null;           // 标志当前列是否为 NULL，true 表示 NULL
        bool error;             // 标志在读取当前列数据时是否发生错误
        enum_field_types type;     // 当前列的数据类型（MySQL 内部类型枚举，如 MYSQL_TYPE_STRING）
        unsigned long length;      // 当前列实际接收到的数据长度（由 MySQL 在查询结果中设置）
        int32_t data_length;       // 当前列所分配的内存大小（即 buffer 长度）
        char* data;                // 指向存储实际数据的缓冲区（由 alloc 分配）
    };

private:
    int m_errno;
    std::string m_errstr;
    //持有关联的 MySQL 预处理语句对象。
    std::shared_ptr<MySQLStmt> m_stmt;
    // 里面的每个 MYSQL_BIND 对象，对应结果集中的一列。
    //MYSQL_BIND 是 MySQL 官方 C API 中定义的结构体，用于 绑定参数或结果数据，它的作用取决于是用于绑定输入（参数）还是输出（结果集）
    std::vector<MYSQL_BIND> m_binds;
    //存储每列的实际数据及元信息。
    std::vector<Data> m_datas;
};

class MySQLManager;
// 这个 MySQL 类是 C++ 中用于封装 MySQL 数据库操作的对象类，它继承自 IDB 接口，提供了对 MySQL 的连接、执行 SQL、事务控制、预处理语句、错误处理等功能的完整支持。
class MySQL : public IDB, public std::enable_shared_from_this<MySQL> {
friend class MySQLManager;
public:
    typedef std::shared_ptr<MySQL> ptr;

    //构造一个MySQL对象
    MySQL(const std::map<std::string, std::string>& args);

    //连接数据库
    bool connect();
    //检测与数据库的连接是否仍然有效
    bool ping();

    //执行格式化的SQL语句，使用可变参数
    virtual int execute(const char* format, ...) override;
    //执行格式化的SQL语句，支持传入可变参数列表
    int execute(const char* format, va_list ap);
    //执行一个完整的SQL字符串
    virtual int execute(const std::string& sql) override;

    ISQLData::ptr query(const char* format, ...) override;
    ISQLData::ptr query(const char* format, va_list ap);
    ISQLData::ptr query(const std::string& sql) override;

    //获取最近一次INSERT操作自动生成的ID(如自增主键)
    //自增主键指定是在表声明时被声明为AUTO_INCREMENT的字段，无需手动插入，他会自己填充自己自增
    int64_t getLastInsertId() override;
    uint64_t getInsertId();
    //获取上一次执行SQL操作影响到行数
    uint64_t getAffectedRows();
    //返回自身mysql对象的shared_ptr,用于保持生命周期或传递对象引用
    std::shared_ptr<MySQL> getMySQL();
    //返回原始MYSQL*指针，用于访问底层MySQL C API
    std::shared_ptr<MYSQL> getRaw();

    //开始一个事务对象
    ITransaction::ptr openTransaction(bool auto_commit) override;
    
    //准备一个SQL语句用于后续绑定参数执行，返回IStmt预处理对象
    sylar::IStmt::ptr prepare(const std::string& sql) override;
    //执行一条预处理语句，并绑定传入的参数，适用于 INSERT/UPDATE/DELETE 等操作。
    template<typename... Args>
    int execStmt(const char* stmt, Args&&... args);
    //执行带参数的预处理查询语句（SELECT），返回结果集。
    template<class... Args>
    ISQLData::ptr queryStmt(const char* stmt, Args&&... args);

    const char* cmd();
    //切换当前连接使用的数据库
    bool use(const std::string& dbname);
    //返回最近一次操作的错误码。
    int getErrno() override;
    //返回最近一次操作的错误字符串信息。
    std::string getErrStr() override;

private:
    // 判断当前 MySQL 连接是否“需要健康检查”（比如是否要调用 ping() 来确认连接还活着）。
    bool isNeedCheck();

private:
    //用于存储数据库连接的相关参数
    //{
    //    "host": "127.0.0.1",
    //    "port": "3306",
    //    "user": "root",
    //    "password": "123456",
    //    "database": "test"
    //}
    std::map<std::string, std::string> m_params;
    //封装底层mysql连接句柄
    std::shared_ptr<MYSQL> m_mysql;
    //记录最近一次执行的SQL命令
    std::string m_cmd;
    //记录当前连接所用到的数据库名
    std::string m_dbname;
    //记录当前对象最后一次被使用的时间戳
    uint64_t m_lastUsedTime;
    //记录当前连接是否出现了不可恢复的错误
    bool m_hasError;
    //表示所属连接池的大小上限
    // 含义：表示某一个具体数据库连接（或数据库连接池）自身的连接数量或容量。
    // 作用：用于跟踪该数据库连接池中创建了多少连接，或者当前使用的连接数。
    // 管理层级：单个数据库连接池的本地统计。
    int32_t m_poolSize;
};

//事务相关类
class MySQLTransaction : public ITransaction {
public:
    typedef std::shared_ptr<MySQLTransaction> ptr;
    static MySQLTransaction::ptr Create(MySQL::ptr mysql, bool auto_commit);
    ~MySQLTransaction();

    bool begin() override;
    bool commit() override;
    bool rollback() override;


    virtual int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<MySQL> getMySQL();

    bool isAutoCommit() const { return m_autoCommit;}
    bool isFinished() const { return m_isFinished;}
    bool isError() const { return m_hasError;}

private:
    MySQLTransaction(MySQL::ptr mysql, bool auto_commit);

private:
    MySQL::ptr m_mysql;
    bool m_autoCommit;
    bool m_isFinished;
    bool m_hasError;
};

// 这个 MySQLStmt 类的作用是对 MySQL 预处理语句（prepared statement） 
// 进行封装，提供更高级、类型安全的接口，让开发者更方便地操作 MySQL 的语句绑定、执行与查询。
class MySQLStmt : public IStmt, public std::enable_shared_from_this<MySQLStmt> {
public:
    typedef std::shared_ptr<MySQLStmt> ptr;
    static MySQLStmt::ptr Create(MySQL::ptr db, const std::string& stmt);

    ~MySQLStmt();

    // 将 C++ 中的数据类型绑定到 MySQL 的预处理语句参数上，用于执行 SQL 语句前向 ? 占位符注入实际的值。
    //每个 bind() 会为不同类型的数据创建相应的 MYSQL_BIND 对象，并存储在一个数组m_binds（std::vector<MYSQL_BIND>）中
    int bind(int idx, const int8_t& value);
    int bind(int idx, const uint8_t& value);
    int bind(int idx, const int16_t& value);
    int bind(int idx, const uint16_t& value);
    int bind(int idx, const int32_t& value);
    int bind(int idx, const uint32_t& value);
    int bind(int idx, const int64_t& value);
    int bind(int idx, const uint64_t& value);
    int bind(int idx, const float& value);
    int bind(int idx, const double& value);
    int bind(int idx, const std::string& value);
    int bind(int idx, const char* value);
    int bind(int idx, const void* value, int len);
    //for nullptr type
    int bind(int index);
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

    int getErrno() override;
    std::string getErrStr() override;

    int execute() override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query() override;

    MYSQL_STMT* getRaw() const { return m_stmt; }

private:
    MySQLStmt(MySQL::ptr db, MYSQL_STMT* stmt);

private:
    MySQL::ptr m_mysql;
    MYSQL_STMT* m_stmt;
    std::vector<MYSQL_BIND> m_binds;
};

//MySQLManager类是一个数据库连接池的管理器，负责管理多个数据库连接并提供一些执行 SQL 语句和事务操作的方法。
class MySQLManager {
public:
    typedef sylar::Mutex MutexType;

    MySQLManager();
    ~MySQLManager();

    //获取一个数据库连接。它会根据传入的 name（连接池中某个数据库连接的标识符）返回一个 MySQL 连接实例的共享指针。
    MySQL::ptr get(const std::string& name);
    //该函数用于注册一个新的数据库连接配置。通过给定的连接名和一组配置参数，
    //函数会将这些信息注册到 MySQLManager 中，以便后续创建和管理数据库连接。
    void registerMySQL(const std::string& name, const std::map<std::string, std::string>& m_params);

    //检查连接池中的连接是有效的
    void checkConnection(int sec = 30);

    //获得/设置连接池最大连接数
    uint32_t getMaxConn() const { return m_maxConn; }
    void setMaxConn(uint32_t v) { m_maxConn = v; }

    //第一个参数 name 在这几个 execute 函数中，用来指定使用哪个数据库连接实例来执行 SQL 操作。
    int execute(const std::string& name, const char* format, ...);
    int execute(const std::string& name, const char* format, va_list ap);
    int execute(const std::string& name, const std::string& sql);
    ISQLData::ptr query(const std::string& name, const char* format, ...);
    ISQLData::ptr query(const std::string& name, const char* format, va_list ap); 
    ISQLData::ptr query(const std::string& name, const std::string& sql);

    //打开事务
    MySQLTransaction::ptr openTransaction(const std::string& name, bool auto_commit);

private:
    //释放 MySQL 连接，并将其放回连接池，或者在连接池已满时删除该连接
    void freeMySQL(const std::string& name, MySQL* m);

private:
    // 最大连接数。如果 m_maxConn = 100，那不管你注册了多少个数据库，每个数据库有多少连接，加起来最多只能存在 100 个连接。
    uint32_t m_maxConn;
    // 用于多线程环境下的互斥锁，保护 m_conns 和 m_dbDefines。
    MutexType m_mutex;
    // 存储每个数据库连接名称对应的连接池（std::list<MySQL*>）。即每个数据库有一个连接池，连接池中包含多个数据库连接对象。
    std::map<std::string, std::list<MySQL*>> m_conns;
    // 存储数据库连接的配置定义，std::map<std::string, std::string> 用于存储配置项。
    std::map<std::string, std::map<std::string, std::string>> m_dbDefines;
};

typedef sylar::Singleton<MySQLManager> MySQLMgr;

//这个 MySQLUtil 工具类的作用是：为应用程序提供简洁统一的方式来执行 MySQL 查询和执行 SQL 语句，
//它作为数据库操作的“门面（Facade）”，封装了底层连接池、语句拼接、异常处理等细节。
class MySQLUtil {
public:
    //假设你在配置文件中定义了多个数据库连接：
    //mysql:
    //   default:
    //     host: localhost
    //     user: root
    //     password: 123456
    //     db: main_db
    //   analytics:
    //     host: 192.168.1.100
    //     user: stat
    //     password: abc123
    //     db: analytics_db
    //那么你可以这样调用：
    //auto res1 = MySQLUtil::Query("default", "SELECT * FROM users");
    static ISQLData::ptr Query(const std::string& name, const char* format, ...);
    static ISQLData::ptr Query(const std::string& name, const char* format,va_list ap); 
    static ISQLData::ptr Query(const std::string& name, const std::string& sql);
    //尝试执行 SQL 语句，失败时最多重试 count 次
    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const char* format, ...);
    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const std::string& sql);

    static int Execute(const std::string& name, const char* format, ...);
    static int Execute(const std::string& name, const char* format, va_list ap); 
    static int Execute(const std::string& name, const std::string& sql);
    static int TryExecute(const std::string& name, uint32_t count, const char* format, ...);
    static int TryExecute(const std::string& name, uint32_t count, const char* format, va_list ap); 
    static int TryExecute(const std::string& name, uint32_t count, const std::string& sql);
};
    

namespace {

// 定义一个递归模板，用于绑定 SQL 语句的多个参数
template<size_t N, typename Head, typename... Tail>
struct MySQLBinder {
    // 绑定当前参数并递归绑定剩余的参数
    static int Bind(MySQLStmt::ptr stmt, const Head& value, Tail&... tail) {
        // 将当前参数绑定到 SQL 语句的第 N 个位置
        int rt = stmt->bind(N, value);
        if (rt != 0) {  // 如果绑定失败，返回错误码
            return rt;
        }
        // 递归继续绑定剩余的参数，索引 N + 1 表示下一个参数的位置
        return MySQLBinder<N + 1, Tail...>::Bind(stmt, tail...);
    }
};

}  // namespace

// bindX 函数，用于开始递归绑定过程
template<typename... Args>
int bindX(MySQLStmt::ptr stmt, Args&... args) {
    // 从参数索引 1 开始递归绑定所有参数
    return MySQLBinder<1, Args...>::Bind(stmt, args...);
}

template<typename... Args>
// execStmt 用于执行带有多个参数的 SQL 语句
int MySQL::execStmt(const char* stmt, Args&&... args) {
    // 创建 MySQLStmt 对象，并准备执行 SQL 语句
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if (!st) {  // 如果创建失败，返回 -1
        return -1;
    }
    
    // 绑定传入的参数
    int rt = bindX(st, args...);
    if (rt != 0) {  // 如果绑定失败，返回绑定错误
        return rt;
    }
    
    // 执行 SQL 语句
    return st->execute();
}

template<class... Args>
// queryStmt 用于执行带有多个参数的 SQL 查询
ISQLData::ptr MySQL::queryStmt(const char* stmt, Args&&... args) {
    // 创建 MySQLStmt 对象，并准备执行 SQL 查询
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if (!st) {  // 如果创建失败，返回空指针
        return nullptr;
    }
    
    // 绑定传入的参数
    int rt = bindX(st, args...);
    if (rt != 0) {  // 如果绑定失败，返回空指针
        return nullptr;
    }
    
    // 执行查询
    return st->query();
}

// 以下是宏定义，用于为不同类型生成特化版本的 MySQLBinder
namespace {
#define XX(type, type2) \
template<size_t N, typename... Tail> \
struct MySQLBinder<N, type, Tail...> { \
    static int Bind(MySQLStmt::ptr stmt, type2 value, Tail&... tail) { \
        int rt = stmt->bind(N, value); \
        if (rt != 0) {  // 如果绑定失败，返回错误码 \
        // 递归绑定剩余的参数                       \             
        return MySQLBinder<N + 1, Tail...>::Bind(stmt, tail...); \
    } \
};    

// 通过宏实例化不同类型的绑定逻辑
XX(char*, char*);          // 处理 char* 类型的参数
XX(const char*, char*);    // 处理 const char* 类型的参数
XX(std::string, std::string&); // 处理 std::string 类型的参数
XX(int8_t, int8_t&);       // 处理 int8_t 类型的参数
XX(uint8_t, uint8_t&);     // 处理 uint8_t 类型的参数
XX(int16_t, int16_t&);     // 处理 int16_t 类型的参数
XX(uint16_t, uint16_t&);   // 处理 uint16_t 类型的参数
XX(int32_t, int32_t&);     // 处理 int32_t 类型的参数
XX(uint32_t, uint32_t&);   // 处理 uint32_t 类型的参数
XX(int64_t, int64_t&);     // 处理 int64_t 类型的参数
XX(uint64_t, uint64_t&);   // 处理 uint64_t 类型的参数
XX(float, float&);         // 处理 float 类型的参数
XX(double, double&);       // 处理 double 类型的参数

#undef XX

}
}
}

#endif