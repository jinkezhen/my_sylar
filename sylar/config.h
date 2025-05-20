/**
 * @file config.h
 * @brief 配置模块
 * @date 2025-03-08
 * @copyright Copyright (c) 2025 kezhen.jin All rights reserved
 */
#ifndef __SYLAR_CONFIG_H__
#define __SYLAR__CONFIG_H__

#include <memory>
#include <functional>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <list>

#include "util.h"

namespace sylar {

/**
 * @brief 配置变量(ConfigVar)的基类：定义了所有配置项目的通用接口
 */
//在sylar框架中每个配置项都是ConfigVarBase的派生类，具体存储不同类型的值，比如int、double、string等
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    
    ConfigVarBase(const std::string& name, const std::string& description = "")
        :m_name(name),
         m_description(description) {
            std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }
    virtual ~ConfigVarBase() {}

    //返回配置参数的名称
    const std::string& getName() const {return m_name;}
    //返回配置参数的描述
    const std::string& getDescription() const {return m_description;}

    //将当前配置项目的值转换成字符串，便于输出或保存到文件
    virtual std::string toString() = 0;
    //通过字符串反序列化并设置当前配置项的值
    virtual bool fromString(const std::string& val) = 0;
    //返回配置参数值的类型名称
    virtual std::string getTypeName() const = 0;

protected:
    //配置参数的名称
    std::string m_name;
    //配置参数的描述
    std::string m_description;
};

/**
 * @brief 通用的类型转换模板类：F原类型->T目标类型
 */
template <class F, class T>
class LexicalCast {
public:
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};

//将YAML格式的字符串解析为std::vector<T>：适用于"[1, 2, 3]"这样的字符串
template <class T>
class LexicalCast<std::string, std::vector<T>> {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        //在模板代码中，如果某个类型依赖于模板参数，且是嵌套类型，必须使用typename
        typename std::vector<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//将std::vector<T>转换成YAML格式的std::string
template <class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//将YAML格式的字符串解析为std::list<T>：适用于"[1, 2, 3]"这样的字符串
template <class T>
class LexicalCast<std::string, std::list<T>> {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//将std::list<T>转换成YAML格式的std::string
template <class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::set<T>)
 */
template<class T>
class LexicalCast<std::string, std::set<T> > {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::set<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::unordered_set<T>)
 */
template<class T>
class LexicalCast<std::string, std::unordered_set<T> > {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_set<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//将std::string类型转为std::map<std::string, T>
template <class T>
class LexicalCast<std::string, std::map<std::string, T>> {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YMAL::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for (auto& it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(), LexicalCast<std::string, T)()(ss.str()));
        }
        return vec;
    }
};

//将std::map<T>转换成YAML格式的std::string
template <class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for (auto&i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::unordered_map<std::string, T>)
 */
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> > {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_map<std::string, T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief ConfigVar是一个模板类，用于存储和管理应用程序的配置信息
 */
//继承自ConfigVarBase，并提供了对配执项的基本操作，如获取值、设置值、序列化(转为字符串)、反序列化(从字符串解析)、监听值的变化等功能
template <class T, class FromStr = LexicalCast<std::string, T>, class ToStr = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase {
public:
    // typedef RWMutex RWMutexType;
    typedef std::shared_ptr<ConfigVar> ptr;
    //该回调函数用于监听配置值的变化
    typedef std::function<void(const T& old_val, const T& new_val)> on_change_cb;

    ConfigVar(const std::string& name, const T& default_value, const std::string& description = "")
        : ConfigVarBase(name, description), m_val(default_value) {}
    
        std::string toString() override {
            try {
                // RWMutexType::ReadLock lock(m_mutex);
                return ToStr()(m_val);
            } catch (std::exception& e) {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception " << e.what()
                                                  << " convert: " << TypeToName<T>() << " to string "
                                                  << " name=" << m_name;
            }
            return "";
        }
        bool fromString(const std::string& val) override {
            try {
                setValure(FromStr()(val));
            } catch (std::exception& e) {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::fromString exception " << e.what()
                                                  << " convert: " << TypeToName<T>() << " to string "
                                                  << " name=" << m_name;
            }
            return false;
        }

        const T getValue() {
            RWMutexType::ReadLock lock(m_mutex);
            return m_val;
        }
        void setValue(const T* v) {
            {
                // RWMutexType::ReadLock lock(m_mutex);
                if (v == m_val) {
                    return;
                }
                for (auto& i : m_cbs) {
                    i.second(m_val, v);  //执行回调函数
                }
            }
            // RWMutexType::WriteLock lock(m_mutex);
            m_val = v;
        }
        std::string getTypeName() const override {return TypeToName<T>();}

        uint64_t addListener(on_change_cb cb) {
            static uint64_t s_fun_id = 0;
            // RWMutexType::WriteLock lock(m_mutex);
            ++s_fun_id;
            m_cbs[s_fun_id] = cb;
            return s_fun_id;
        }

        void delListener(uint64_t key) {
            // RWMutexType::WriteLock lock(m_mutex);
            m_cbs.erase(key);
        }

        on_change_cb getListener(uint64_t key) {
            // RWMutexType::ReadLock lock(m_mutex);
            auto it = m_cbs.find(key);
            return it == m_cbs.end() ? nullptr : it->second;
        }

        void clearListener() {
            RWMutexType::WriteLock lock(m_mutex);
            m_cbs.clear();
        }
private:
    // RWMutexType m_mutex;
    T m_val;
    //变更回调函数组，uint64_t key要求唯一，一般可以用hash
    std::map<uint64_t, on_change_cb> m_cbs;
};

//该类是一个全局配置管理器，所有的COnfigVar配置都由Config统一管理，他存储了所有的配置项
//通过Lookup方法获取或创建配置项，使得整个应用成雪可以动态管理配置
//该类是一个单例类，他的成员函数大部分是static，表示这些函数可以在不实例化Config的情况下使用
class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    // typedef RWMutex RWMutexType;

    //查找配置项，如果不存在则创建配置项
    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name, const T& default_value, const std::string& description = "") {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas.find(name);
        if (it != GetDatas.end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
            if (tmp) {
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exist";
                return tmp;
            }
            //处理类型不匹配的情况
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exist but type not "
                                              << TypeToName<T>() << " real type is "
                                              << it->second->getTypeName()
                                              << " " << it->second->toString();
            return nullptr;
        }

        //不存在则创建该配置项
        if (name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid" << name;
            throw std::invalid_argument(name);
        } 

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    //lookup的重载形式，之查找不创建
    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas.end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }

    //从YAML配置文件中加载配置项，并存入ConfigVarMap，root是YAML解析后的根节点，里面包含所有配置信息
    static void LoadFromYaml(const YAML::Node& root);
    //批量加载path文件夹中的Yaml配置文件，force为true时，强制更新所有配置项(即使已经有值)
    static void LoadFromConfDir(const std::string& path, bool force = false);
    //查找指定名称的配置项，但返回的是ConfigVarBase::ptr基类指针，而不是ConfigVar<T>,适用于不知道配置项类型的情况
    static ConfigVarBase::ptr LookupBase(const std::string& name);
    //遍历所有配置项，并对每个配置项执行cb这个回调函数
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);


private:
    //返回所有配置项
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }
    //返回配置项的RWMutex
    // static RWMutexType& GetMutex() {
    //     static RWMutexType s_mutex;
    //     return s_mutex;
    // }
};
}

#endif // !__SYLAR_CONFIG_H__#define __SYLAR__CONFIG_H__

