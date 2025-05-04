#include "util.h"
#include <cxxabi.h>
#include <cstdarg>
#include <execinfo.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <sstream>
#include <string.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <signal.h>
#include <arpa/inet.h>
#include <algorithm>
#include <ifaddrs.h>
#include <jsoncpp/json/json.h>
#include <yaml-cpp/yaml.h>

#include "log.h"
#include "fiber.h"

namespace sylar{

pid_t GetThreadId() {
    return (pid_t)syscall(SYS_gettid);
}

uint32_t GetFiberId() {
    return sylar::Fiber::GetFiberId();
}


//demangle：将c++编译后的函数名恢复成可读的格式，
//          因为c++代码在编译时，函数名会被修饰，变得很难读
std::string demangle(const char* str) {
    size_t size = 0;   // 存储解码后的字符串大小（但这里并没有真正使用）
    int status = 0;    // 存储 demangle 的返回状态（0 表示成功）
    std::string rt;
    rt.resize(256);    // 预分配 256 个字符的空间，用于存储解析出的符号名

    // 尝试从 str 解析出 mangled name
    if(1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
        // 使用 GCC 提供的 abi::__cxa_demangle 进行符号解码
        char* v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if(v) {  // 如果解码成功
            std::string result(v);  // 将解码后的字符串存入 result
            free(v);  // 释放由 abi::__cxa_demangle 分配的内存
            return result;  // 返回可读的符号名
        }
    }

    // 如果上面的解析失败，尝试直接提取函数名
    if(1 == sscanf(str, "%255s", &rt[0])) {
        return rt;  // 直接返回解析出的字符串
    }

    // 如果两次解析都失败，直接返回原始字符串
    return str;
}

void Backtrack(std::vector<std::string> bt, int size, int skip) {
    void** array = (void**)malloc(sizeof(void*) * size);
    //用于获取当前调用栈的函数调用地址，存入 array 中，最多存储 size 个地址，返回实际获取的地址数量
    size_t s = ::backtrace(array, size);
    //将 array 中的地址转换为可读的字符串（通常是函数名和偏移量），返回字符串数组，需手动 free() 释放。
    char** strings = backtrace_symbols(array, s);
    if (strings == NULL) {
        // SYLAR_LOG_ERROR(g_logger) << "backtrace_symbols error";
        return;
    }
    for (int i = skip; i < size; ++i) {
        bt.push_back(demangle(strings[i]));
    }

    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string& prefix) {
    std::vector<std::string> bt;
    Backtrack(bt, size, skip);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) {
        //return format:[Item] apple
            //          [Item] banana
            //          [Item] cherry
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

uint64_t GetCurrentMS() {
    /*
    struct timeval {
        time_t tv_sec;   // 秒（从 1970-01-01 00:00:00 UTC 以来的秒数）
        suseconds_t tv_usec;  // 微秒（0 - 999999）
    };
    */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

std::string Time2Str(time_t ts, const std::string& format) {
    struct tm tm;
    localtime_r(&ts, &tm);
    char buf[64];
    //按照指定格式 format，将 tm 结构体中的时间信息格式化为字符串，并存储到 buf 中。
    strftime(buf, sizeof(buf), format.c_str(), &tm);
    return buf;
}

time_t Str2Time(const char* str, const char* format) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    //按照格式format解析字符串str到结构体tm中
    if (!strptime(str, format, &tm)) {
        return 0;
    }
    //将tm结构体转换为time_t时间戳
    return mktime(&tm);
}

std::string ToUpper(const std::string& name) {
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::toupper);
}

std::string ToLower(const std::string& name) {
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::tolower);
}

//递归遍历指定目录及其子目录，获取所有符合后缀suffix条件的文件，并将文件路径存入files中
void FSUtil::ListAllFile(std::vector<std::string>& files, const std::string& path, const std::string& suffix) {
    //检查文件是否存在
    if (access(path.c_str(), 0) != 0) return;

    //打开目录
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) return;

    //遍历目录中的所有文件和子目录()
    struct dirent* dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        //处理子目录
        if (dp->d_type == DT_DIR) {
            //dp可能是当前目录，也可能是上级目录，直接跳过
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
                continue;
            }
            ListAllFile(files, path + "/" + dp->d_name, suffix);
        } else if (dp->d_type == DT_REG) {  //如果是普通文件
            std::string filename(dp->d_name);
            if (suffix.empty()) {
                files.push_back(path + "/" + filename);
            } else {
                if (filename.size() < suffix.size()) continue;
                if (filename.substr(filename.size() - suffix.size()) == suffix) {
                    files.push_back(path + "/" + filename);
                }
            }
        }
    }

    //关闭目录
    closedir(dir);
}

/** 
 * @brief 获取文件的元数据（大小，权限，类型等），并支持软连接
 * @param[in] file 要查询的文件路径
 * @param[out] st 存储查询到的文件的元数据
 */
//静态全局函数：只能在当前cpp中使用
static int __lstat(const char* file, struct stat* st = nullptr) {
    struct stat lst;
    int ret = lstat(file, &lst);
    if (st) {
        *st = lst;
    }
    return ret;
}

/**
 * @brief 用于创建目录，具备检查目录是否存在的功能，避免重复创建
 * @param[in] dirname 要创建的目录路径
 */
static int __mkdir(const char* dirname) {
    if (access(dirname, F_OK) == 0) return 0;
    return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

//递归创建多级目录，他会逐级检查目录是否存在，并逐层创建目录，直到整个目录创建完成
bool FSUtil::Mkdir(const std::string& dirname) {
    //检查目录是否已经存在
    if (__lstat(dirname.c_str()) == 0) return true;

    char* path = strdup(dirname.c_str());
    //在path中从第二个字符开始查找第一个/:找到路径的层级分隔符，从而逐级创建目录
    char* ptr = strchr(path + 1, '/');
    do {
        for (; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) {
            *ptr = '\0';
            if (__mkdir(path) != 0) {
                break;
            }
            //处理最终目录
            if (ptr != nullptr) {
                break;
            } else if (__mkdir(path) != 0) {
                break;
            }
        }
        free(path);
        return true;
    } while (0);
    free(path);
    return false;
}

//进程id文件(pidfile)：用于记录某个正在运行进程id的文件，通常守护进程会在启动时创建pidfile，
//                    写入自己的进程id，以便后续管理
//pidfile作用：
//1.防止重复启动：通过pidfile记录PID，下一次启动时先检查是否都有相同进程在运行，避免重复启动
//2.进程管理：其他进程或脚本可以读取pidfile获取PID，通过kill命令或信号控制进程
//3.日志、监控：监控系统可以读取pidfile确保进程仍在运行，否则触发重启
//注意：一般对于单实例进程来讲，一个pidfile中只存一个pid；对于多个子进程的情况，
//     pidifle管理主进程加子进程（主进程PID记录在pidfile中，子进程不记录；
//     或者pidfile 里存多个 PID，每个进程一行）

//用于判断pidfile中的进程是否正在运行
bool FSUtil::IsRunningPidfile(const std::string& pidfile) {
    //检查pidfile是否存在
    if (__lstat(pidfile.c_str()) != 0) return false;
    //尝试打开pidfile
    std::ifstream ifs(pidfile);
    std::string line;
    if (!ifs || !std::getline(ifs, line)) return false;
    if (line.empty()) return false;
    pid_t pid = atoi(line.c_str());
    if (pid <= 1) return false;
    //检查pid是否存在，这个kill并不会杀死进程，而是用于检查进程是否存在
    //信号值 0 并不会真的发送信号，而是用于检查进程是否存在且有权限访问。
    if (kill(pid, 0) != 0) return false;
    //进程存在
    return true;  
}

//删除指定文件，若 exist 为 false，则在文件存在时才尝试删除。
bool FSUtil::Unlink(const std::string& filename, bool exist) {
    if (!exist && __lstat(filename.c_str())) return true;
    return ::unlink(filename.c_str()) == 0; 
}

bool FSUtil::Rm(const std::string& path) {
    struct stat st;
    //文件或目录不存在
    if (lstat(path.c_str(), &st)) return true;
    //判断是否是普通文件，是普通文件直接删除
    if (!(st.st_mode & S_IFDIR)) {
        return Unlink(path);
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    bool ret = true;
    struct dirent* dp = nullptr;
    while (dp == readdir(dir)) {
        //特殊目录直接跳过
        if (!strcmp(dp -> d_name, ".") || !strcmp(dp -> d_name, "..")) continue;
        std::string dirname = path + "/" + dp -> d_name;
        ret = ret && Rm(dirname);
    }
    closedir(dir);
    if (::rmdir(path.c_str())) {
        ret = false;
    }
    return ret;
}

bool FSUtil::Mv(const std::string& from, const std::string& to) {
    //从from->to需要先删除to
    if (!Rm(to)) return false;
    return rename(from.c_str(), to.c_str()) == 0;
}

bool FSUtil::Realpath(const std::string& path, std::string& rpath) {
    if (__lstat(path.c_str())) return false;
    char* ptr = ::realpath(path.c_str(), nullptr);
    if (nullptr == ptr) return false;
    std::string(ptr).swap(rpath);
    free(ptr);
    return true;
}   

bool FSUtil::Symlink(const std::string& from, const std::string& to) {
    if (!Rm(to)) return false;
    return ::symlink(from.c_str(), to.c_str());
}

std::string FSUtil::Dirname(const std::string& filename) {
    if (filename.empty()) return "";
    //rfind:用于从字符串的末尾向前查找某个字符或子串的位置。
    auto pos = filename.rfind('/');
    if (pos == 0) {
        return "/";
    } else if (pos == std::string::npos) {
        return ".";
    } else {
        return filename.substr(0, pos);
    }
}

std::string FSUtil::Basename(const std::string& filename) {
    if (filename.empty()) return filename;
    auto pos = filename.rfind('/');
    if (pos == std::string::npos) {
        return filename;
    } else {
        return filename.substr(pos + 1);
    }
}

bool FSUtil::OpenForRead(std::ifstream& ifs, const std::string& filename, std::ios_base::openmode mode) {
    ifs.open(filename.c_str(), mode);
    return ifs.is_open();
}

bool FSUtil::OpenForWrite(std::ofstream& ofs, const std::string& filename, std::ios_base::openmode mode) {
    ofs.open(filename.c_str(), mode);
    if (!ofs.is_open()) {
        //如果文件打开失败，先尝试创建文件所在的目录
        std::string dir = Dirname(filename);
        Mkdir(dir);
        ofs.open(filename.c_str(), mode);
    }
    return ofs.is_open();
}

// str转char：只转字符串中下标为0的字符
int8_t TypeUtil::ToChar(const std::string& str) {
    if (str.empty()) return 0;
    return *str.begin();
}

// str转int64_t：将字符串按十进制转为整数，失败返回0
int64_t TypeUtil::Atoi(const std::string& str) {
    if (str.empty()) return 0;
    return strtoull(str.c_str(), nullptr, 10);
}

// str转double：将字符串转为浮点数，失败返回0.0
double TypeUtil::Atof(const std::string& str) {
    if (str.empty()) return 0;
    return atof(str.c_str());
}

// c风格字符串转char：只取第一个字符，空指针返回0
int8_t TypeUtil::ToChar(const char* str) {
    if (str == nullptr) return 0;
    return str[0];
}

// c风格字符串转int64_t：按十进制转换，失败返回0
int64_t TypeUtil::Atoi(const char* str) {
    if (str == nullptr) return 0;
    return strtoul(str, nullptr, 10);
}

// c风格字符串转double：转换失败返回0.0
double TypeUtil::Atof(const char* str) {
    if (str == nullptr) return 0;
    return atof(str);
}

/**
 * @brief 格式化字符串，支持可变参数
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 格式化后的 std::string
 */
static std::string Format(const char* fmt, ...) {
    va_list ap;                 // 定义可变参数列表
    va_start(ap, fmt);          // 初始化可变参数列表
    auto v = Formatv(fmt, ap);  // 调用 Formatv 处理
    va_end(ap);                 // 结束可变参数处理
    return v;
}

/**
 * @brief 格式化字符串，使用 va_list 版本
 * @param fmt 格式化字符串
 * @param ap  可变参数列表
 * @return 格式化后的 std::string
 */
static std::string Formatv(const char* fmt, va_list ap) {
    char* buf = nullptr;
    auto len = vasprintf(&buf, fmt, ap);  // 申请动态缓冲区并格式化字符串
    if (len == -1) return "";            // 失败时返回空字符串
    std::string res(buf, len);           // 复制到 std::string
    free(buf);                           // 释放动态分配的内存
    return res;                          // 这里原代码 `return ret;` 可能是笔误
}

static const char uri_chars[256] = {
    /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 0, 0,
    /* 64 */
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    /* 128 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static const char xdigit_chars[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define CHAR_IS_UNRESERVED(c)           \
    (uri_chars[(unsigned char)(c)])

std::string StringUtil::UrlEncode(const std::string& str, bool space_as_plus) {
    static const char *hexdigits = "0123456789ABCDEF";
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str() ; c < end; ++c) {
        if(!CHAR_IS_UNRESERVED(*c)) {
            if(!ss) {
                ss = new std::string;
                ss->reserve(str.size() * 1.2);
                ss->append(str.c_str(), c - str.c_str());
            }
            if(*c == ' ' && space_as_plus) {
                ss->append(1, '+');
            } else {
                ss->append(1, '%');
                ss->append(1, hexdigits[(uint8_t)*c >> 4]);
                ss->append(1, hexdigits[*c & 0xf]);
            }
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::UrlDecode(const std::string& str, bool space_as_plus) {
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str(); c < end; ++c) {
        if(*c == '+' && space_as_plus) {
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, ' ');
        } else if(*c == '%' && (c + 2) < end
                    && isxdigit(*(c + 1)) && isxdigit(*(c + 2))){
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
            c += 2;
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

/**
 * @brief 去除字符串两端的指定字符（默认为空格
 * @param 需要进行修整的字符串
 * @param 指定的字符集，函数会去掉字符串两端属于这个字符集的字符
 */
std::string StringUtil::Trim(const std::string& str, const std::string& delimit) {
    //找到字符串str中第一个不属于delimit字符集的字符的位置
    auto begin = str.find_first_not_of(delimit);
    if (begin == std::string::npos) {
        return "";
    }
    auto end = str.find_last_not_of(delimit);
    return str.substr(begin, end - begin + 1);
}

//去除字符串左端指定字符
std::string StringUtil::TrimLeft(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if (begin == std::string::npos) return "";
    return str.substr(begin);
}

//去除字符串右端指定字符
std::string StringUtil::TrimRight(const std::string& str, const std::string& delimit) {
    auto end = str.find_last_not_of(delimit);
    if (end == std::string::npos) return "";
    return str.substr(0, end);
}

//wstring：是c++中用来表示unicode字符集的字符串类型，通常使用wchar_t类型的字符，
//他能够支持多字节字符(如中文、日文等非ASCII字符)，每个字符通常占用2或4个字节。
//与此相比string是基于char类型的字符串数组，通常只支持单字节字符集(如ASCII)，每个字符占用一字节

std::string StringUtil::WStringToString(const std::wstring& ws) {
    //将当前系统的locale设置加载到程序中，返回当前的locale设置并将其存储在str_locale中，LC_ALL表示设置所有的locale分类
    std::string str_locale = setlocale(LC_ALL, "");
    const wchar_t* wch_src = ws.c_str();
    //第一次调用得到将宽字符串wch_src转换为多字节字符串char类型所需的大小
    size_t dest_size = wcstombs(NULL, wch_src, 0) + 1;
    char* ch_dest = new char[dest_size];
    memset(ch_dest, 0, dest_size);
    //第二次调用wcstombs才真将宽字符串转为多字节字符串
    wcstombs(ch_dest, wch_src, dest_size);
    std::string str_result = ch_dest;
    delete []ch_dest;
    //恢复原始的locale设置
    setlocale(LC_ALL, str_locale.c_str());
    return str_result;
}

std::wstring StringUtil::StringToWString(const std::string& s) {
    std::string str_locale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t n_dest_size = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wch_dest = new wchar_t[n_dest_size];
    wmemset(wch_dest, 0, n_dest_size);
    mbstowcs(wch_dest, chSrc, n_dest_size);
    std::wstring wstr_result = wch_dest;
    delete []wch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return wstr_result;
}

//获取当前的主机名
std::string GetHostName() {
    std::shared_ptr<char> host(new char[512], sylar::delete_array<char>);
    memset(host.get(), 0, 512);
    gethostname(host.get(), 512);
    return host.get();
}

//获取当前机器的ipv4地址
in_addr_t GetIPv4Inet() {
    //ifaddrs结构体存储网络接口信息
    struct ifaddrs* ifas = nullptr;
    struct ifaddrs* ifa = nullptr;
    in_addr_t localhost = inet_addr("127.0.0.1");
    if (getifaddrs(&ifas)) {
        // SYLAR_LOG_ERROR(g_logger) << "getifaddrs errno=" << errno
        //     << " errstr=" << strerror(errno);
        return localhost;
    }
    in_addr_t ipv4 = localhost;
    for (ifa = ifas; ifa && ifa -> ifa_addr; ifa = ifa->ifa_next) {
        //检查接口是否是IPv4地址
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        //跳过名为lo开头的接口(回环接口，通常为127.0.0.1)
        if (!strncasecmp(ifa->ifa_name, "lo", 2)) {
            continue;
        }
        ipv4 = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
        if (ipv4 == localhost) continue;
    }
    if (ifas != nullptr) freeifaddrs(ifas);
    return ipv4;
}

//获取ipv4的字符串形式
std::string _GetIPv4() {
    std::shared_ptr<char> ipv4(new char[INET_ADDRSTRLEN], sylar::delete_array<char>);
    memset(ipv4.get(), 0, INET_ADDRSTRLEN);
    auto ia = GetIPv4Inet();
    //将ipv4地址转为字符串
    inet_ntop(AF_INET, &ia, ipv4.get(), INET_ADDRSTRLEN);
    return ipv4.get();
}

std::string GetIPv4() {
    //声明为static保证ip变量只被初始化一次，并且在之后的所有调用中使用同一个值(第一次调用得到的值)，从而避免重复计算，降低性能开销，并保证每次调用结果一致
    static const std::string ip = _GetIPv4();
    return ip;
}

//yaml结构：是一种人类可读的数据格式，常用于配置文件
//yaml支持三种数据类型：
//1.标量scalar：单个值，如字符串、整数、浮点数等
//2.序列sequence：类似数组的结构，用-表示多个元素
//3.映射map：类似字典，使用key::value形式存储键值对
// name: 张三
// age: 25
// hobbied:
//     - 篮球
//     - 读书
// address:
//     city: 北京
//     zip: 100000
//is_student: false

//json结构：轻量级数据交换格式
//json支持三种数据类型
//1.标量：如字符串、整数、布尔值等
//2.数组：使用[]包裹多个元素
//3.对象：使用{}存储键值对，键必须是字符串
//{
//     "name": "张三",
//     "age": 25,
//     "hobbies": ["篮球", "读书"],
//     "address": {
//         "city": "北京",
//         "zip": 100000
//     }
//}

//json和yaml的格式转换函数
bool YamlToJson(const YAML::Node& ynode, Json::Value& jnode) {
    try {
        //处理标量
        if (ynode.IsScalar()) {
            Json::Value v(ynode.Scalar());
            jnode.swapPayload(v);
            return true;
        }
        //处理序列
        if (ynode.IsSequence()) {
            for (size_t i = 0; i < ynode.size(); ++i) {
                Json::Value v;
                if (YamlToJson(ynode[i], v)) {
                    jnode.append(v);
                } else {
                    return false;
                }
            }
        } //处理映射表
        else if (ynode.IsMap()) {
            for (auto it = ynode.begin(); it != ynode.end(); ++it) {
                Json::Value v;
                if (YamlToJson(it->second, v)) {
                    jnode[it->first.Scalar()] = v;
                } else {
                    return false;
                }
            }
        }
    } catch(...) {
        return false;
    }
    return true;
}

bool JsonToYaml(const Json::Value& jnode, YAML::Node& ynode) {
    try {
        //处理数组
        if (jnode.isArray()) {
            for (int i = 0; i < (int)jnode.size(); ++i) {
                YAML::Node n;
                if (JsonToYaml(jnode[i], n)) {
                    ynode.push_back(n);
                } else {
                    return false;
                }
            }
        } else if (jnode.isObject()) { //处理对象
            for (auto it = jnode.begin(); it != jnode.end(); ++it) {
                YAML::Node n;
                if (JsonToYaml(*it, n)) {
                    ynode[it.name()] = n;
                } else {
                    return false;
                }
            }
        } else {  //处理标量
            ynode = jnode.asString();
        }
    } catch (...) {
        return false;
    }
    return true;
}


//在protobuf中为未知字段指的是接收端没有定义，但仍然出现在消息中的字段
//protobuf允许在解析时忽略未识别的字段，并将其存储在UnknowFieldSet中，而不是直接丢弃

//用于将UnknowFieldSet转为json
static void serialize_unknowfieldset(const google::protobuf::UnknownFieldSet& ufs, Json::Value& jnode) {
    //int为字段编号(protobuf中的tag)，std::vector<Json::Value代表该字段的多个值
    std::map<int, std::vector<Json::Value>> kvs;
    for (int i = 0; i < ufs.field_count(); ++i) {
        //获取每一个未知字段
        const auto& uf = ufs.field(i);
        switch((int)uf.type()) {
            case google::protobuf::UnknownField::TYPE_VARINT:
                kvs[uf.number()].push_back((Json::Int64)uf.varint());
                break;
            case google::protobuf::UnknownField::TYPE_FIXED32:
                kvs[uf.number()].push_back((Json::UInt)uf.fixed32());
                break;
            case google::protobuf::UnknownField::TYPE_FIXED64:
                kvs[uf.number()].push_back((Json::UInt64)uf.fixed64());
                break;
            case google::protobuf::UnknownField::TYPE_LENGTH_DELIMITED:     //处理字符串或者嵌套的protobuf消息
                google::protobuf::UnknownFieldSet tmp;
                auto& v = uf.length_delimited();
                if (!v.empty() && tmp.ParseFromString(v)) {
                    Json::Value vv;
                    serialize_unknowfieldset(tmp, vv);
                    kvs[uf.number()].push_back(vv);
                } else {
                    kvs[uf.number()].push_back(v);
                }
                break;
        }
        //组装json结构
        for (auto& kv : kvs) {
            //如果某个field number对应多个值，则以数组形式存入json
            if (kv.second.size() > 1) {
                for (auto& n : kv.second) {
                    jnode[std::to_string(kv.first)].append(n);
                }
            } else {
                jnode[std::to_string(kv.first)] = kv.second[0];
            }
        }
    }
}

//该函数用于将protobuf消息对象转换为JSON
static void serialize_message(const google::protobuf::Message& message, Json::Value& jnode) {
    //获取message的源信息
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    //reflection用于动态访问protobuf当前字段的值
    const google::protobuf::Reflection* reflection = message.GetReflection();
    //遍历所有字段
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = descriptor->field(i);
        //此时reflection与field这个字段绑定，在下一下遍历时，reflection又与下一个field绑定
        if (field->is_repeated()) {
            //如果是repeated（数组）字段， 但值为空，则跳过
            if (!reflection->FieldSize(message, field)) continue;
        } else {
            //如果是非repeated（单个值）字段，但未被设置，则跳过
            if (!reflection->HasField(message, field)) continue;
        }
        //处理repeated字段
        if (field->is_repeated()) {
            switch(field->cpp_type()) {
//通过宏展开，避免重复的代码块
#define XX(cpptype, method, valuetype, jsontype) \
            case google::protobuf::FieldDescriptor::CPPTYPE_##cpptype: { \
                int size = reflection->FieldSize(message, field); \
                for (int n = 0; n < size; ++n) { \
                    jnode[field->name()].append((jsontype)reflection->GetRepeated##method(message, field, n)); \
                } \
                break; \
            }
        //宏调用
        XX(INT32, Int32, int32_t, Json::Int);
        XX(UINT32, UInt32, uint32_t, Json::UInt);
        XX(FLOAT, Float, float, double);
        XX(DOUBLE, Double, double, double);
        XX(BOOL, Bool, bool, bool);
        XX(INT64, Int64, int64_t, Json::Int64);
        XX(UINT64, UInt64, uint64_t, Json::UInt64);
#undef XX
                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                    int size = reflection->FieldSize(message, field);
                    for(int n = 0; n < size; ++n) {
                        jnode[field->name()].append(reflection->GetRepeatedEnum(message, field, n)->number());
                    }
                    break;
                }
                case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
                    int size = reflection->FieldSize(message, field);
                    for(int n = 0; n < size; ++n) {
                        jnode[field->name()].append(reflection->GetRepeatedString(message, field, n));
                    }
                    break;
                }
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                    int size = reflection->FieldSize(message, field);
                    for(int n = 0; n < size; ++n) {
                        Json::Value vv;
                        serialize_message(reflection->GetRepeatedMessage(message, field, n), vv);
                        jnode[field->name()].append(vv);
                    }
                    break;
                    }
                }
                continue;
            }

            switch(field->cpp_type()) {
#define XX(cpptype, method, valuetype, jsontype) \
        case google::protobuf::FieldDescriptor::CPPTYPE_##cpptype: { \
            jnode[field->name()] = (jsontype)reflection->Get##method(message, field); \
            break; \
        }
        XX(INT32, Int32, int32_t, Json::Int);
        XX(UINT32, UInt32, uint32_t, Json::UInt);
        XX(FLOAT, Float, float, double);
        XX(DOUBLE, Double, double, double);
        XX(BOOL, Bool, bool, bool);
        XX(INT64, Int64, int64_t, Json::Int64);
        XX(UINT64, UInt64, uint64_t, Json::UInt64);
#undef XX
                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                jnode[field->name()] = reflection->GetEnum(message, field)->number();
                break;
                }
                case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
                jnode[field->name()] = reflection->GetString(message, field);
                break;
                }
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                serialize_message(reflection->GetMessage(message, field), jnode[field->name()]);
                break;
                }
        }
    }
    const auto& ufs = reflection->GetUnknownFields(message);
    serialize_unknowfieldset(ufs, jnode);
}

//protobuf数据转为json形式的字符串
std::string PBToJsonString(const google::protobuf::Message& message) {
    Json::Value jnode;
    serialize_message(message, jnode);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "    ";
    std::string result = Json::writeString(writer, jnode);
    return result;
}

SpeedLimit::SpeedLimit(uint32_t speed) 
    : m_speed(speed)
    , m_countPerMS(0)
    , m_curCount(0)
    , m_curSec(0) {
    if (speed == 0) {
        m_speed = (uint32_t)-1;
    }
    //计算每毫秒允许的最大操作次数
    m_countPerMS = m_speed / 1000;
}

//用于控制请求或任务的执行速率
//使用示例：对于下面这个例子来说在1s内，最多只能执行add(1)10次，也就是在这个for循环中1s内只能执行到i = 9，然后会usleep直到下一个秒周期到来
// SpeedLimit limiter(10); // 每秒最多 10 次
// for (int i = 0; i < 20; ++i) {
//     limiter.add(1);  // 每次调用代表执行了一次操作
//     std::cout << "操作执行：" << i + 1 << std::endl;
// }
void SpeedLimit::add(uint32_t v) {
    uint64_t cur_ms = sylar::GetCurrentMS();
    //判断当前是否是新的1s时间窗口，如果进入新的1s则更新计数器
    if (cur_ms / 1000 != m_curSec) {
        m_curSec = cur_ms / 1000;
        m_curCount = v;
        return;
    }
    //将本次新增的v次加到m_currCount中
    m_curCount += v;

    //计算当前秒内实际已经消耗的时间
    int usedems = cur_ms % 1000;
    //计算当前秒内理论上应该消耗的时间
    int limitms = m_curCount / m_countPerMS;
    //如果当前执行速度过快，需要等待
    if (usedems < limitms) {
        usleep(1000 * (limitms - usedems));
    }
}

bool ReadFixFromStreamWithSpeed(std::istream& is, char* data, const uint64_t& size, const uint64_t speed) {
    SpeedLimit::ptr limit;
    //is可以是cin标准输入流，也可以是ifstream文件流
    //检查is是否是文件流
    if (dynamic_cast<std::ifstream*>(&is)) {
        limit.reset(new SpeedLimit(speed));
    }
    //记录已经读取的数据量
    uint64_t offset = 0;
    //每次读取的数据块大小
    uint64_t per = std::max((uint64_t)ceil(speed / 100.0), (uint64_t)1024 * 64);
    while (is && (offset < size)) {
        uint64_t s = size - offset > per ? per : (size - offset);
        is.read(data + offset, s);
        offset += is.gcount();
        //限速
        if (limit) {
            limit->add(is.gcount());
        }
    }
    return offset == size;
}

bool WriteFixFromStreamWithSpeed(std::ostream& os, const char* data, const uint64_t& size, const uint64_t& speed) {
    SpeedLimit::ptr limit;
    if (dynamic_cast<std::ofstream*>(&os)) {
        limit.reset(new SpeedLimit(speed));
    }
    uint64_t offset = 0;
    //最小也要64kb防止频繁的io操作
    uint64_t per = std::max((uint64_t)ceil(speed / 100.0), (uint64_t)1024 * 64);
    while (os && (offset < size)) {
        uint64_t s = size - offset > per ? per : size - offset;
        os.write(data + offset, s);
        offset += s;
        if (limit) {
            limit->add(s);
        }
    }
    return offset == size;
}



}