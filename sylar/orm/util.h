#ifndef __SYALR_ORM_UTIL_H__
#define __SYALR_ORM_UTIL_H__

#include <tinyxml2.h>
#include <string>

namespace syalr {
namespace orm {

// 将输入字符串转换为变量命名格式（如：my_variable_name）
std::string GetAsVariable(const std::string& v);

// 将输入字符串转换为类名格式（如：MyClassName）
std::string GetAsClassName(const std::string& v);

// 将输入字符串转换为成员变量命名格式（如：m_variableName）
std::string GetAsMemberName(const std::string& v);

// 将输入字符串转换为 getter 函数名（如：getVariableName）
std::string GetAsGetFunName(const std::string& v);

// 将输入字符串转换为 setter 函数名（如：setVariableName）
std::string GetAsSetFunName(const std::string& v);

// 将 XML 节点转换为字符串表示形式（用于调试或保存）
std::string XmlToString(const tinyxml2::XMLNode& node);

// 将输入字符串转换为宏定义格式（如：_DEFINE_）
std::string GetAsDefineMacro(const std::string& v);
}
}

#endif __SYALR_ORM_UTIL_H__