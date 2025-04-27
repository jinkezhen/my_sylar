/**
 * @file stream.h
 * @brief 流接口
 * @date 2025-04-22
 * @copyright Copyright (c) 2025年 All rights reserved
 */

#ifndef __SYLAR_STREAM_H__
#define __SYLAR_STREAM_H__

#include <memory>
#include "bytearray.h"

namespace sylar {

//流结构
class Stream {
public:
    typedef std::shared_ptr<Stream> ptr;

    virtual ~Stream(){}

    /**
     * @brief 读数据
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @return 
     *      @retval >0 返回接收到的数据的实际大小
     *      @retval =0 被关闭
     *      @retval <0 出现流错误
     */
    virtual int read(void* buffer, size_t length) = 0;

    /**
     * @brief 读数据
     * @param[out] ba 接收数据的ByteArray
     * @param[in] length 接收数据的内存大小
     * @return 
     *      @retval >0 返回接收到的数据的实际大小
     *      @retval =0 被关闭
     *      @retval <0 出现流错误
     */
    virtual int read(ByteArray::ptr ba, size_t length) = 0;

    //读固定长度的数据
    virtual int readFixSize(void* buffer, size_t length);
    virtual int readFixSize(ByteArray::ptr ba, size_t length);


    //写,返回写入数据的实际大小
    virtual int write(const void* buffer, size_t length) = 0;
    virtual int write(ByteArray::ptr ba, size_t length) = 0;
    virtual int writeFixSize(const void* buffer, size_t length);
    virtual int writeFixSize(ByteArray::ptr ba, size_t length);

    //关闭流
    virtual void close() = 0;
};



}

#endif