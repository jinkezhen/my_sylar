#include "mysql.h"
#include "sylar/log.h"
#include "sylar/config.h"


namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("syste");
static sylar::ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_mysql_dbs = 
        sylar::Config::Lookup("mysql.dbs", std::map<std::string, std::map<std::string, std::string>>(), "mysql dbs");

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts) {
    struct tm tm;
    ts = 0;
    localtime_r(&ts, &tm); //使用ts初始化tm，目的是初始化tm
    tm.tm_year = mt.year - 1900;
    tm.tm_mon = mt.month - 1;
    tm.tm_mday = mt.day;
    tm.tm_hour = mt.hour;
    tm.tm_min = mt.minute;
    tm.tm_sec = mt.second;
    ts = mktime(&tm);    //将tm转为时间戳
    if (ts < 0) ts = 0;
    return true;
}

bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt) {
    struct tm tm;
    localtime_r(&ts, &tm);
    mt.year = tm.tm_year + 1900;
    mt.month = tm.tm_mon + 1;
    mt.day = tm.tm_mday;
    mt.hour = tm.tm_min;
    mt.minute = tm.tm_min;
    mt.second = tm.tm_sec;
    return true;
}

namespace {
    struct MySQLThreadIniter {
        MySQLThreadIniter() {
            mysql_thread_init();
        }
        ~MySQLThreadIniter() {
            mysql_thread_end();
        }
    };
}

static MYSQL* mysql_init(std::map<std::string, std::string>& params, const int& timeout) {
    static thread_local MySQLThreadIniter s_thread_initer;

    MYSQL* mysql = ::mysql_init(nullptr);
    if (mysql == nullptr) {
        SYLAR_LOG_ERROR(g_logger) << "mysql_init error";
    }

    if (timeout > 0) {
        mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    }
    bool close = false;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &close);
    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    int port = sylar::GetParamValue(params, "port", 0);
    std::string host = sylar::GetParamValue<std::string>(params, "host");
    std::string user = sylar::GetParamValue<std::string>(params, "user");
    std::string passwd = sylar::GetParamValue<std::string>(params, "passwd");
    std::string dbname = sylar::GetParamValue<std::string>(params, "dbname");

    if (mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str()
                            ,dbname.c_str(), port, NULL, 0) == nullptr) {
                                SYLAR_LOG_ERROR(g_logger) << "mysql_real_connect(" << host
                                << ", " << port << ", " << dbname
                                << ") error: " << mysql_error(mysql);
        mysql_close(mysql);
        return nullptr;                        
    }
    return mysql;
}

MySQL::MySQL(const std::map<std::string, std::string>& args)
                :m_params(args)
                ,m_lastUsedTime(0)
                ,m_hasError(false)
                ,m_poolSize(10) {
}

bool MySQL::connect() {
    if (!m_mysql && !m_hasError) {
        return true;
    }
    MYSQL* m = mysql_init(m_params, 0);
    if (!m) {
        m_hasError = true;
        return false;
    }
    m_hasError = false;
    m_poolSize = sylar::GetParamValue(m_params, "pool", 5);
    m_mysql.reset(m, mysql_close);
    return true;
}

sylar::IStmt::ptr MySQL::prepare(const std::string& sql) {
    return MySQLStmt::Create(shared_from_this(), sql);
}

ITransaction::ptr MySQL::openTransaction(bool auto_commit) {
    return MySQLTransaction::Create(shared_from_this(), auto_commit);
}

int64_t MySQL::getLastInsertId() {
    return mysql_insert_id(m_mysql.get());
}

bool MySQL::isNeedCheck() {
    if (time(0) - m_lastUsedTime < 5 && !m_hasError) {
        return false;
    } 
    return true;
}

bool MySQL::ping() {
    if (!m_mysql) {
        return false;
    }
    if (mysql_ping(m_mysql.get())) {
        m_hasError = true;
        return false;
    }
    m_hasError = false;
    return true;
}

// 支持 printf 风格的 SQL 执行函数   mysql.execute("INSERT INTO users(name, age) VALUES('%s', %d)", "Tom", 18);
int MySQL::execute(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int MySQL::execute(const char* format, va_list ap) {
    m_cmd = sylar::StringUtil::Formatv(format, ap);
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if (r) {
        SYLAR_LOG_ERROR(g_logger) << "cmd= " << cmd() << "error=" << getErrStr();
        m_hasError = true;
    } else {
        m_hasError = false;
    }
    return r;
}

int MySQL::execute(const std::string& sql) {
    m_cmd = sql;
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if(r) {
        SYLAR_LOG_ERROR(g_logger) << "cmd=" << cmd()
            << ", error: " << getErrStr();
        m_hasError = true;
    } else {
        m_hasError = false;
    }
    return r;
}

//这是为了在不拥有资源所有权时，安全地构造一个 shared_ptr，但又 避免重复析构或 delete this 的风险。
// 也就是说：
// MySQL::getMySQL() 返回的是一个指向自己的 shared_ptr；
// 但它不能也不应该销毁自己，因为这个对象可能由外部管理（比如连接池）；
// 所以要让 shared_ptr 的析构器啥都不干（用 nop 函数）。
std::shared_ptr<MySQL> MySQL::getMySQL() {
    return MySQL::ptr(this, sylar::nop<MySQL>);
}

std::shared_ptr<MYSQL> MySQL::getRaw() {
    return m_mysql;
}

uint64_t MySQL::getAffectedRows() {
    if(!m_mysql) {
        return 0;
    }
    return mysql_affected_rows(m_mysql.get());
}

static MYSQL_RES* my_mysql_query(MYSQL* mysql, const char* sql) {
    if (mysql == nullptr) {
        return nullptr;
    }
    if (sql == nullptr) {
        return nullptr;
    }
    if (::mysql_query(mysql, sql)) {
        return nullptr;
    }
    MYSQL_RES* res = mysql_store_result(mysql);
    if (res == nullptr) {
        SYLAR_LOG_ERROR(g_logger) << mysql_error(mysql);
    }
    return res;
}

ISQLData::ptr MySQL::query(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = query(format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr MySQL::query(const char* format, va_list ap) {
    m_cmd = sylar::StringUtil::Formatv(format, ap);
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) {
        m_hasError = true;
        return nullptr;
    }
    m_hasError = false;
    ISQLData::ptr rt(new MySQLRes(res, mysql_errno(m_mysql.get()), mysql_error(m_mysql.get())));
    return rt;
}

ISQLData::ptr MySQL::query(const std::string& sql) {
    m_cmd = sql;
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) {
        m_hasError = true;
        return nullptr;
    }
    m_hasError = false;
    ISQLData::ptr rt(new MySQLRes(res, mysql_errno(m_mysql.get())
                        ,mysql_error(m_mysql.get())));
    return rt;
}

const char* MySQL::cmd() {
    return m_cmd.c_str();
}

bool MySQL::use(const std::string& dbname) {
    if (!m_mysql) {
        return false;
    }
    if (m_dbname == dbname) {
        return true;
    }
    if (mysql_select_db(m_mysql.get(), dbname.c_str()) == 0) {
        m_dbname = dbname;
        m_hasError = false;
        return true;
    } else {
        m_dbname = "";
        m_hasError = true;
        return false;
    }
}

std::string MySQL::getErrStr() {
    if(!m_mysql) {
        return "mysql is null";
    }
    const char* str = mysql_error(m_mysql.get());
    if(str) {
        return str;
    }
    return "";
}

int MySQL::getErrno() {
    if(!m_mysql) {
        return -1;
    }
    return mysql_errno(m_mysql.get());
}

uint64_t MySQL::getInsertId() {
    if(m_mysql) {
        return mysql_insert_id(m_mysql.get());
    }
    return 0;
}

MySQLRes::MySQLRes(MYSQL_RES* res, int eno, const char* estr) 
    : m_errno(eno), m_errstr(estr), m_cur(nullptr), m_curLength(nullptr) {
    if (res) {
        //mysql_free_result 是 MySQL C API 提供的一个函数，用于释放由 mysql_store_result() 或 mysql_use_result() 
        //返回的 MYSQL_RES* 指针占用的内存资源。
        m_data.reset(res, mysql_free_result);
    }
}

bool MySQLRes::foreach(data_cb cb) {
    MYSQL_ROW row;
    uint64_t fields = getColumnCount();
    int i = 0;
    //mysql_fetch_row 是 MySQL C API 的函数，用来从结果集中依次获取下一行数据。
    while ((row = mysql_fetch_row(m_data.get()))) {
        if (!cb(row, fields, i++)) {
            break;
        }
    }
    return true;
}

int MySQLRes::getDataCount() {
    return mysql_num_rows(m_data.get());
}

int MySQLRes::getColumnCount() {
    return mysql_num_fields(m_data.get());
}

int MySQLRes::getColumnBytes(int idx) {
    return m_curLength[idx];
}

int MySQLRes::getColumnType(int idx) {
    return 0;
}

std::string MySQLRes::getColumnName(int idx) {
    return "";
}

bool MySQLRes::isNull(int idx) {
    if (m_cur[idx] == nullptr) {
        return true;
    }
    return false;
}

int8_t MySQLRes::getInt8(int idx) {
    return getInt64(idx);
}

uint8_t MySQLRes::getUint8(int idx) {
    return getInt64(idx);
}

int16_t MySQLRes::getInt16(int idx) {
    return getInt64(idx);
}

uint16_t MySQLRes::getUint16(int idx) {
    return getInt64(idx);
}

int32_t MySQLRes::getInt32(int idx) {
    return getInt64(idx);
}

uint32_t MySQLRes::getUint32(int idx) {
    return getInt64(idx);
}

int64_t MySQLRes::getInt64(int idx) {
    return sylar::TypeUtil::Atoi(m_cur[idx]);
}

uint64_t MySQLRes::getUint64(int idx) {
    return getInt64(idx);
}

float MySQLRes::getFloat(int idx) {
    return getDouble(idx);
}

double MySQLRes::getDouble(int idx) {
    return sylar::TypeUtil::Atof(m_cur[idx]);
}

std::string MySQLRes::getString(int idx) {
    return std::string(m_cur[idx], m_curLength[idx]);
}

std::string MySQLRes::getBlob(int idx) {
    return std::string(m_cur[idx], m_curLength[idx]);
}

time_t MySQLRes::getTime(int idx) {
    if (!m_cur[idx]) {
        return 0;
    }
    return sylar::Str2Time(m_cur[idx]);
}

bool MySQLRes::next() {
    // 每次调用：MYSQL_ROW row = mysql_fetch_row(res);
    //MySQL 内部就会：把“当前行”的数据取出；自动把“当前行指针”向下一行移动。
    m_cur = mysql_fetch_row(m_data.get());
    m_curLength = mysql_fetch_lengths(m_data.get());
    return m_cur;
}

// 函数名	                             所属模块	   返回类型	                     作用说明
// mysql_stmt_errno(stmt)	            预处理语句	unsigned int	 返回最近一次与 stmt 相关操作的错误码（错误号）。
// mysql_stmt_error(stmt)	            预处理语句	const char*	     返回最近一次错误的描述字符串（错误信息）。
// mysql_stmt_result_metadata(stmt)	    预处理语句	MYSQL_RES*	     获取语句执行后的字段元数据（字段名、类型等）。
// mysql_num_fields(res)	            结果元数据	unsigned int	 返回结果集中字段（列）的数量。
// mysql_fetch_fields(res)	            结果元数据	MYSQL_FIELD*	 返回包含所有字段描述信息的数组指针。
// mysql_stmt_bind_result(stmt, binds)	预处理语句	int（0 表示成功） 绑定结果缓冲区，将查询结果绑定到内存结构中供取出。
// mysql_stmt_execute(stmt)	            预处理语句	int	             执行预处理语句。
// mysql_stmt_store_result(stmt)	    预处理语句	int	             将结果集从服务器读取到客户端缓冲区中（支持多次读取）。
MySQLStmtRes::ptr MySQLStmtRes::Create(std::shared_ptr<MySQLStmt> stmt) {
    // 获取语句的错误码与错误信息（即使没有错误也调用）
    int eno = mysql_stmt_errno(stmt->getRaw());
    const char* errstr = mysql_stmt_error(stmt->getRaw());

    // 创建一个 MySQLStmtRes 实例，先填入当前状态
    MySQLStmtRes::ptr rt(new MySQLStmtRes(stmt, eno, errstr));

    // 如果已有错误，直接返回错误信息封装的结果对象
    if (eno) {
        return rt;
    }

    // 获取结果集的元数据（字段信息）
    MYSQL_RES* res = mysql_stmt_result_metadata(stmt->getRaw());
    if (!res) {
        // 获取失败，说明语句不返回结果（如 INSERT），返回错误对象
        return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
    }

    // 获取字段数量和字段信息
    int num = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);

    // 为绑定数组和数据缓存数组分配空间
    rt->m_binds.resize(num);
    rt->m_datas.resize(num); // ✅ 必须有这行，避免 m_datas[i] 越界
    memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * num);

    for (int i = 0; i < num; ++i) {
        // 保存字段类型
        rt->m_datas[i].type = fields[i].type;

        // 按字段类型分配合适的内存空间
        switch(fields[i].type) {
#define XX(m, t) \
            case m: \
                rt->m_datas[i].alloc(sizeof(t)); \
                break;
            XX(MYSQL_TYPE_TINY, int8_t);
            XX(MYSQL_TYPE_SHORT, int16_t);
            XX(MYSQL_TYPE_LONG, int32_t);
            XX(MYSQL_TYPE_LONGLONG, int64_t);
            XX(MYSQL_TYPE_FLOAT, float);
            XX(MYSQL_TYPE_DOUBLE, double);
            XX(MYSQL_TYPE_TIMESTAMP, MYSQL_TIME);
            XX(MYSQL_TYPE_DATETIME, MYSQL_TIME);
            XX(MYSQL_TYPE_DATE, MYSQL_TIME);
            XX(MYSQL_TYPE_TIME, MYSQL_TIME);
#undef XX
            default:
                // 其它类型（如字符串），按字段最大长度分配内存
                rt->m_datas[i].alloc(fields[i].length);
                break;
        }

        // 设置绑定结构（从 switch 外统一设置）
        rt->m_binds[i].buffer_type   = rt->m_datas[i].type;
        rt->m_binds[i].buffer        = rt->m_datas[i].data;
        rt->m_binds[i].buffer_length = rt->m_datas[i].data_length;
        rt->m_binds[i].length        = &rt->m_datas[i].length;
        rt->m_binds[i].is_null       = &rt->m_datas[i].is_null;
        rt->m_binds[i].error         = &rt->m_datas[i].error;
    }

    // 绑定结果字段到客户端缓存
    if (mysql_stmt_bind_result(stmt->getRaw(), &rt->m_binds[0])) {
        return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
    }

    // 执行语句
    stmt->execute();

    // 把所有结果缓存到客户端（便于后续读取）
    if (mysql_stmt_store_result(stmt->getRaw())) {
        return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
    }

    // 成功，返回封装好字段绑定与数据结构的结果对象
    return rt;
}

int MySQLStmtRes::getDataCount() {
    return mysql_stmt_num_rows(m_stmt->getRow());
}

int MySQLStmtRes::getColumnCount() {
    return mysql_stmt_field_count(m_stmt->getRow());
}

int MySQLStmtRes::getColumnBytes(int idx) {
    return m_datas[idx].length;
}

int MySQLStmtRes::getColumnType(int idx) {
    return m_datas[idx].type;
}

std::string MySQLStmtRes::getColumnName(int idx) {
    return "";
}

bool MySQLStmtRes::isNull(int idx) {
    return m_datas[idx].is_null;
}

// m_datas[idx].data	是一个 char* 指针，指向从数据库中读取的某一列的原始字节数据。
// (type*)...	把 char* 转换成 type*，即转换成你想要读取的数据类型的指针（如 int32_t*）。
// *(type*)...	对这个转换后的指针进行 解引用，也就是取出它所指向的数据值。
#define XX(type) \
    return *(type*)m_datas[idx].data

int8_t MySQLStmtRes::getInt8(int idx) {
    XX(int8_t);
}

uint8_t MySQLStmtRes::getUint8(int idx) {
    XX(uint8_t);
}

int16_t MySQLStmtRes::getInt16(int idx) {
    XX(int16_t);
}

uint16_t MySQLStmtRes::getUint16(int idx) {
    XX(uint16_t);
}

int32_t MySQLStmtRes::getInt32(int idx) {
    XX(int32_t);
}

uint32_t MySQLStmtRes::getUint32(int idx) {
    XX(uint32_t);
}

int64_t MySQLStmtRes::getInt64(int idx) {
    XX(int64_t);
}

uint64_t MySQLStmtRes::getUint64(int idx) {
    XX(uint64_t);
}

float MySQLStmtRes::getFloat(int idx) {
    XX(float);
}

double MySQLStmtRes::getDouble(int idx) {
    XX(double);
}
#undef XX

std::string MySQLStmtRes::getString(int idx) {
    return std::string(m_datas[idx].data, m_datas[idx].length);
}

std::string MySQLStmtRes::getBlob(int idx) {
    return std::string(m_datas[idx].data, m_datas[idx].length);
}

time_t MySQLStmtRes::getTime(int idx) {
    MYSQL_TIME* v = (MYSQL_TIME*)m_datas[idx].data;
    time_t ts = 0;
    mysql_time_to_time_t(*v, ts);
    return ts;
}

bool MySQLStmtRes::next() {
    return !mysql_stmt_fetch(m_stmt->getRaw());
}


MySQLStmtRes::Data::Data()
    :is_null(0)
    ,error(0)
    ,type()
    ,length(0)
    ,data_length(0)
    ,data(nullptr) {
}

MySQLStmtRes::Data::~Data() {
    if(data) {
        delete[] data;
    }
}

void MySQLStmtRes::Data::alloc(size_t size) {
    if(data) {
        delete[] data;
    }
    data = new char[size]();
    length = size;
    data_length = size;
}

MySQLStmtRes::MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno
                           ,const std::string& estr)
    :m_errno(eno)
    ,m_errstr(estr)
    ,m_stmt(stmt) {
}

MySQLStmtRes::~MySQLStmtRes() {
    if(!m_errno) {
        mysql_stmt_free_result(m_stmt->getRaw());
    }
}


}