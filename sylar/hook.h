//hook(钩子)是一种编程技术，允许我们在程序运行时修改函数的默认行为。
//hook的主要作用
//1.功能增强：修改某些API的行为，例如实现超时控制、异步IO等
//2.安全控制：拦截关键系统调用，防止恶意操作
//hook技术一般通过函数指针重定向，或者动态链接库劫持实现，使得调用某些函数时，不是执行原始的系统调用，而是执行我们自己定义的“替代函数”

/**
 * @file hook.h
 * @brief hook函数封装
 * @date 2025-03-23
 * @copyright Copyright (c) 2025 All rights reserved
 */

 #ifndef __SYLAR_HOOK_H__
 #define __SYLAR_HOOL_H__

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

namespace sylar {

    //检查当前线程是否启用了hook机制
    bool is_hook_enable();
    //设置当前线程的hook状态，启用或关闭hook机制
    void set_hook_enable(bool flag);

//extern "C"：让c++代码能够正确链接到c语言符号，避免c++的名字修饰导致找不到符号
//c++存在名字修饰，即编译器会将函数名和变量名进行名字修饰，也称为符号重整，他的作用是支持函数重载，命名空间，模板等特性
//由于c++的名字修饰c语言不支持，所以可能会导致c与c++互相调用函数时找不到正确的符号
//解决办法就是使用extern “C”，这样在extern "C的{}作用域内的函数名不会进行名字修饰
//按照现在这个场景来讲就是{}中的c函数，如sleep、write、read等函数不会被名字修饰，因此其他c语言或者动态链接时可以找到正确的符号

extern "C" {

//sleep
//定义一个函数指针，这个函数指针可以指向sleep这种函数
//unsigned int为返回值类型
//sleep_fun为一个函数指针类型
//(unsigned int)函数的参数列表
typedef unsigned int (*sleep_fun)(unsigned int);    
//extern声明一个外部变量，表示变量sleep_f不是在这个.h文件中定义的，而是在别的文件中(hook.cpp中)
//sleep_fun表示这个外部变量是sleep_fun函数指针类型
extern sleep_fun sleep_f;
//usleep
typedef int (*usleep_fun)(useconds_t usec);
extern usleep_fun usleep_f;
//nanosleep
typedef int (*nanosleep_fun)(const struct timespec* req, struct timespec* rem);
extern nanosleep_fun nanosleep_f;

// socket 相关函数指针
typedef int (*socket_fun)(int domain, int type, int protocol);
extern socket_fun socket_f;  
// 对应 socket()，用于创建套接字（socket），返回一个文件描述符（fd）

typedef int (*connect_fun)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern connect_fun connect_f;  
// 对应 connect()，用于将套接字连接到指定的地址（服务器）

typedef int (*accept_fun)(int s, struct sockaddr *addr, socklen_t *addrlen);
extern accept_fun accept_f;  
// 对应 accept()，用于接受客户端连接，返回一个新的套接字描述符

// read 相关函数指针
typedef ssize_t (*read_fun)(int fd, void *buf, size_t count);
extern read_fun read_f;  
// 对应 read()，从文件描述符 fd 读取数据到 buf，最多读取 count 字节

typedef ssize_t (*readv_fun)(int fd, const struct iovec *iov, int iovcnt);
extern readv_fun readv_f;  
// 对应 readv()，使用分散读取（scatter read），从 fd 读取数据到多个缓冲区

typedef ssize_t (*recv_fun)(int sockfd, void *buf, size_t len, int flags);
extern recv_fun recv_f;  
// 对应 recv()，用于从 socket 读取数据，类似 read()，但可以指定额外的标志（flags）

typedef ssize_t (*recvfrom_fun)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_fun recvfrom_f;  
// 对应 recvfrom()，用于接收 UDP 数据包，并可获取发送方的地址信息

typedef ssize_t (*recvmsg_fun)(int sockfd, struct msghdr *msg, int flags);
extern recvmsg_fun recvmsg_f;  
// 对应 recvmsg()，用于接收带有多个缓冲区和控制信息的消息（如带外数据）

// write 相关函数指针
typedef ssize_t (*write_fun)(int fd, const void *buf, size_t count);
extern write_fun write_f;  
// 对应 write()，向文件描述符 fd 写入数据，最多写入 count 字节

typedef ssize_t (*writev_fun)(int fd, const struct iovec *iov, int iovcnt);
extern writev_fun writev_f;  
// 对应 writev()，使用分散写入（gather write），将多个缓冲区的数据写入 fd

typedef ssize_t (*send_fun)(int s, const void *msg, size_t len, int flags);
extern send_fun send_f;  
// 对应 send()，向 socket 发送数据，类似 write()，但可以指定额外的标志（flags）

typedef ssize_t (*sendto_fun)(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
extern sendto_fun sendto_f;  
// 对应 sendto()，用于发送 UDP 数据包，并指定目标地址

typedef ssize_t (*sendmsg_fun)(int s, const struct msghdr *msg, int flags);
extern sendmsg_fun sendmsg_f;  
// 对应 sendmsg()，用于发送带有多个缓冲区和控制信息的消息（如带外数据）

// 关闭文件描述符
typedef int (*close_fun)(int fd);
extern close_fun close_f;  
// 对应 close()，用于关闭文件描述符，释放资源

// 其他控制相关函数指针
typedef int (*fcntl_fun)(int fd, int cmd, ... /* arg */ );
extern fcntl_fun fcntl_f;  
// 对应 fcntl()，用于对文件描述符执行控制操作，如设置非阻塞模式

typedef int (*ioctl_fun)(int d, unsigned long int request, ...);
extern ioctl_fun ioctl_f;  
// 对应 ioctl()，用于设备控制，执行各种 I/O 操作（如获取网络接口信息）

typedef int (*getsockopt_fun)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern getsockopt_fun getsockopt_f;  
// 对应 getsockopt()，用于获取 socket 选项（如超时、缓冲区大小）

typedef int (*setsockopt_fun)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern setsockopt_fun setsockopt_f;  
// 对应 setsockopt()，用于设置 socket 选项（如超时、缓冲区大小）

// 自定义的 connect()，带超时时间，防止连接操作长时间阻塞
extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms);

}

}
#endif