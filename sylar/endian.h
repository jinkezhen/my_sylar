/**
* @file endian.h
* @brief 字节序转换操作类(大端小端)
* @date 2025-03-29
* @copyright Copytright (c) All rights reserved
*/

//该文件提供了一些字节序操作函数，主要用于在不同的字节序(大端和小端)系统中进行数据的字节序切换
//通过模拟函数实现了对不同字节发小的数据类型(8字节、4字节、2字节)进行字节序转换操作。
//不同的处理器架构使用不同的字节序，比如x86架构使用小端字节序，而网络协议一般规定使用大端字节序。
//当数据需要在不同的架构之间传输时，必须进行字节序的转换


//字节序：指的是多字节数据在内存中的存储顺序
//大端字节序：数据的高字节数据存储在低地址，低字节数据存储在高地址
//以数据0x12345678为例
//  地址	数据
//  0x00	0x12
//  0x01	0x34
//  0x02	0x56
//  0x03	0x78
//小端字节序：数据的低字节数据存储在低地址，高字节数据存储在高地址
//以数据0x12345678为例
//  地址	数据
//  0x00	0x78
//  0x01	0x56
//  0x02	0x34
//  0x03	0x12

#ifndef __SYLAR_ENDIAN_H__
#define __SYLAR_ENDIAN_H__

//定义字节序类型：大端和小端
#define SYLAR_LITTLE_ENDIAN 1
#define SYLAR_BIG_ENDIAN 2

#include <type_traits>
#include <byteswap.h>
#include <stdint.h>

namespace sylar {

	//std::enable_if介绍
	//他是c++11引入的，在头文件<type_trait>中，用于启用或者禁用某些模板的实例化
	//template<bool Condition, type T = void>
	//struct enable_if;
	//如果Condition==true,则enable_if::type存在，等于T，否则type不会定义，导致模板匹配失败

	//c++17引入if constexpr允许我们在编译期直接进行条件判断，从而简化代码
	//是一种编译时条件判断机制，用于模板元编程，可以让编译器在编译期决定是否编译某一段代码，而不会导致编译错误
	//只有在条件为真时才会编译这个分支
	//它和普通if-else的区别就是普通if-else会将所有分支都编译，但只执行为true的分支，而if constexpt只将为true的分支进行编译，别的分支不进行编译
	//示例
	//template<typename T>
	//void checkType(T value) {
	//	if constexpr (std::is_integral<T>::value) {
	//		std::cout << value << "是整数类型" << std::endl;
	//	}
	//	else if constexpr (std::is_floating_point<T>::value) {
	//		std::cout << value << "是浮点数类型" << std::endl;
	//	}
	//	else {
	//		std::cout << "未知类型" << std::endl;
	//	}
	//}
	//int maim() {
	//	注意：在c++中模板函数并不是在定义时立即编译，而是在具体调用时(即实例化时才会编译)，
	//		  这个两个调用会分别实例化checkType<int>和checkType<double>，编译器会怎对不同的T生成不同的代码
	//	checkType(4);  实例化时才被编译，实际展开为void checkType<int>(int value){std::cout << value << "是整数类型"<<std::endl;}
	//	checkType(3.14); 实例化时才被编译，实际展开为void checkType<double>(double value) {std::cout << value << "是double类型" << std::endl;}
	//  checkType("test"); 实例化时才被编译，实际展开为void checkType<double>(T value) {std::cout << value << "是未知类型" << std::endl;}
	//}
	
	
	//8字节类型字节序抓换模板函数：可以从大端字节序转为小端字节序或从小端字节序转为大端字节序，取决于最初是什么字节序
	template<class T>
	typename std::enable_if<sizeof(T) == sizeof(uint64_t), T>::type
		byteswap(T value) {
		return (T)bswap_64((uint64_t)value);
	}

	//if con
	//4字节
	template<class T>
	typename std::enable_if<sizeof(T) == sizeof(uint32_t), T>::type
		byteswap(T value) {
		return (T)bswap_32((uint32_t)value);
	}

	//2字节
	template<class T>
	typename std::enable_if<sizeof(T) == sizeof(uint16_t), T>::type
		byteswap(T value) {
		return (T)bswap_16((uint16_t)value);
	}

	//判断当前平台的字节序
#if BYTE_ORDER == BIG_ENDIAN
#define SYLAR_BYTE_ORDER SYLAR_BIG_ENDIAN
#else
#define SYLAR_BYTE_ORDER SYLAR_LITTLE_ENDIAN
#endif

//大端机器
#if SYLAR_BYTE_ORDER == SYLAR_BIG_ENDIAN

	template<class T>
	//如果是大端机器，调用转换为小端字节序时需要调用下byteswap
	T byteswapOnLittleEndian(T t) {
		return byteswap(t);
	}
	//在大端机器上执行交换字节
	template<class T>
	//如果是大端机器，调用转换为大端字节序时直接返回即可
	T byteswapOnBigEndian(T t) {
		return t;
	}
	//小端机器
#else
	template<T t>
	//如果是小端机器，调用转换为小端字节序时直接返回即可
	T byteswapOnLittleEndian(T t) {
		return t;
	}
	tempplate<class T>
		//如果是小端机器，调用转换为大端字节序时需要调用下byteswap
		T byteswapOnBigEndian(T t) {
		return byteswap(t);
	}

#endif

}

#endif
