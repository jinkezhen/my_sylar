/**
 * @file json_util.h
 * @brief json util封装
 * @date 2025-05-03
 */

#ifndef __SYLAR_UTIL_JSON_UTIL_H__
#define __SYLAR_UTIL_JSON_UTIL_H__

#include <string>
#include <iostream>
#include <json/json.h>

namespace sylar {

/**
 * @brief Json 工具类，封装了常见的 JSON 处理接口
 */
class JsonUtil {
public:
    /**
     * @brief 判断字符串是否包含需要转义的字符（如引号、反斜杠等）
     * @param v 输入字符串
     * @return true 表示需要转义，false 表示不需要
     */
    static bool NeedEscape(const std::string& v);

    /**
     * @brief 对字符串中的特殊字符进行转义（如引号、换行、制表符等）
     * @param v 输入字符串
     * @return 返回转义后的字符串
     */
    static std::string Escape(const std::string& v);

    /**
     * @brief 从 JSON 对象中获取 string 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值（字段不存在或类型不符时返回）
     * @return 对应字段的字符串值
     */
    static std::string GetString(const Json::Value& json,
                                 const std::string& name,
                                 const std::string& default_value = "");

    /**
     * @brief 从 JSON 对象中获取 double 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值
     * @return 对应字段的 double 值
     */
    static double GetDouble(const Json::Value& json,
                            const std::string& name,
                            double default_value = 0);

    /**
     * @brief 从 JSON 对象中获取 int32_t 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值
     * @return 对应字段的 int32 值
     */
    static int32_t GetInt32(const Json::Value& json,
                            const std::string& name,
                            int32_t default_value = 0);

    /**
     * @brief 从 JSON 对象中获取 uint32_t 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值
     * @return 对应字段的 uint32 值
     */
    static uint32_t GetUint32(const Json::Value& json,
                              const std::string& name,
                              uint32_t default_value = 0);

    /**
     * @brief 从 JSON 对象中获取 int64_t 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值
     * @return 对应字段的 int64 值
     */
    static int64_t GetInt64(const Json::Value& json,
                            const std::string& name,
                            int64_t default_value = 0);

    /**
     * @brief 从 JSON 对象中获取 uint64_t 类型的字段值
     * @param json JSON 对象
     * @param name 字段名
     * @param default_value 默认值
     * @return 对应字段的 uint64 值
     */
    static uint64_t GetUint64(const Json::Value& json,
                              const std::string& name,
                              uint64_t default_value = 0);

    /**
     * @brief 将 JSON 格式的字符串解析为 Json::Value 对象
     * @param json 输出参数，解析结果存储在此对象中
     * @param v 输入字符串（JSON 格式）
     * @return true 表示解析成功，false 表示失败
     */
    static bool FromString(Json::Value& json, const std::string& v);

    /**
     * @brief 将 Json::Value 对象序列化为 JSON 字符串
     * @param json 输入 JSON 对象
     * @return 返回格式化后的字符串
     */
    static std::string ToString(const Json::Value& json);
};

}

#endif
