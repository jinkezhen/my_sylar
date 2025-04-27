#include "sylar/config.h"
// #include "sylar/env.h"
#include "sylar/util.h"
// #include <sys/types>
#include <sys/stat.h>
#include <unistd.h>
#include <list>
#include <string>
#include "sylar/log.h"

namespace sylar {

//获取名为system的日志器
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    // RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//递归遍历YAML配置文件中的所有结点，将每个节点的路径(键名)和对应的YAML::Node存入输出列表中
static void ListAllMember(const std::string& prefix, const YAML::Node& node,
                            std::list<std::pair<std::string, const YAML::Node>>& output) {
    if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
        // SYLAR_LOG_ERROR(g_logger) << "Config invalid name " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    //递归遍历子节点
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar() : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

//加载YAML配置项
void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);
    for (auto& i : all_nodes) {
        std::string key = i.first;
        //跳过空键名
        if (key.empty()) {
            continue;
        }
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        //查找配置项:如果存在就返回配置项指针
        ConfigVarBase::ptr var = LookupBase(key);
        if (var) {
            var->fromString(i.second.Scalar());
        } else {
            std::stringstream ss;
            ss << i.second;
            var->fromString(ss.str());
        }
    }
}

//存储文件路径到文件最后修改时间的映射
static std::map<std::string, uint64_t> s_file2modifytime;
// static sylar::Mutex s_mutex;

//批量加载配置文件
void Config::LoadFromConfDir(const std::string& path, bool force) {
    //将路径转为绝对路径
    // std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    // FSUtil::ListAllFile(files, absoulte_path, ".yml");
    for (auto& i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);
            // sylar::Mutex::Lock lock(s_mutex);
            //如果force为false并且文件的最后修改时间与s_file2modifytime中记录的时间相同，则跳过该文件，不重新加载
            //假设files中有个文件叫1.yaml,在第一次调用LoadFromConfDir会将1.yaml解析到Config的配置maps中，
            //但第二次再次调用LoadFromConfDir这个函数的时候，如果传入的还是上次1.yaml存在的路径，
            //如果传入的是false，则也不会再解析1.yaml到Config的配置列表中
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            //如果force为true，则强制更新所有配置项
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            // SYLAR_LOG_INFO(g_logger) << "LoadConfFile file=" << i << " ok;";
        } catch (...) {
            // SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file=" << i << " failed";
        }
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    // RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& maps = GetDatas();
    for (auto it = maps.begin(); it != maps.end(); ++it) {
        cb(it->second);
    }
}

}
