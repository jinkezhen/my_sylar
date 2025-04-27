/**
* @brief 网络地址的封装(IPV4 IPV6 UNIX)
* @date 2025-03-29
* @copyright Copyright (c) All rights reserved.
*/

//IPv4：是32位长度的二进制数(2的32次方，约43亿个地址)，通常以四个十进制数表示，每个数字用.分隔，每个数字的范围是0-255  192.168.3.4
//IPv6：是128位长度的二进制数(2的128次方，约340万万万亿个地址)，表示为8组4个十六进制数，每组之间用:分隔	 2001:0db8:85a3:0000:0000:8a2e:0370:7334  

#ifndef __SYLAR_ADDRESS_H__
#define __SYLAR_ADDRESS_H__

#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <map>

namespace sylar {

class IPAddress;

class Address {
public:
	typedef std::shared_ptr<Address> ptr;

	/**
	* @brief 通过sockaddr指针创建Address
	* @param[in] addr sockaddr指针
	* @param[in] addrlen sockaddr的长度
	* @return 返回和sockaddr相匹配的Address，失败返回nullptr
	*/
	static Address::ptr Create(const sockaddr* addr, socklen_t addrlen);

	/**
	* @brief 通过host地址返回对应条件的所有Address
	* @param[out] result 保存所有满足条件的address
	* @param[in] host 域名，服务器名等。可能的格式 仅域名：www.sylar.top；域名带端口号：www.sylar.top:80；仅ip地址：192.168.1.1；ip加端口号：192.168.1.1：8080
	* @param[in] family 协议族（AF_INET,AF_INET6,AF_UNIX）
	* @param[in] type socket类型SOCK_STREAM SOCK_DGRAM
	* @param[in] protocol 协议IPPROTO_TCP IPPROTO_UDP
	* @return 返回是否转换成功
	*/
	static bool Lookup(std::vector<Address::ptr>& result, const std::string& host, int family = AF_INET, int type = 0, int protocol = 0);

	//通过host地址返回对应条件的任意一个Address
	static Address::ptr LookupAny(const std::string& host, int family = AF_INET, int type = 0, int protocol = 0);

	//通过host地址返回对应条件的任意IPAddress
	static std::shared_ptr<IPAddress> LookupAnyIPAddress(const std::string& host, int family = AF_INET, int type = 0, int protocol = 0);

	/**
	* @brief 返回本机所有网卡的网卡名、地址、子网掩码位数
	* @param[out] result 保存本机所有地址
	* @param[in] family 协议族(AF_INET, AF_INET6, AF_UNIX)
	* @return 是否获取成功
	*/
	static bool GetInterfaceAddresses(std::multimap<std::string, std::pair<Address::ptr, uint32_t>>& result, int family = AF_INET);

	/**
	* @brief 获取指定网卡的地址和子网掩码位数
	* @param[out] result 保存指定网卡所有地址
	* @param[in] iface 网卡名称
	* @param[in] family 协议族(AF_INET, AF_INET6, AF_UNIX)
	* @return 是否获取成功
	*/
	static bool GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>>& result, const std::string& iface, int family = AF_INET);

	virtual ~Address(){}

	//返回协议族
	int getFamily() const;

	//返回只读sockaddr指针
	virtual const sockaddr* getAddr() const = 0;

	//返回可读写的sockaddr指针
	virtual sockaddr* getAddr() = 0;

	//返回sockaddr的长度
	virtual socklen_t getAddrLen() const = 0;

	//将Address类的对象转为可读的输出形式，方便查看Address对象的内容，insert和toString功能类似，但输出方式不同，一个是通过流输出，一个是通过字符串输出
	virtual std::ostream& insert(std::ostream& os) const = 0;
	std::string toString() const;

	bool operator<(const Address& rhs) const;
	bool operator==(const Address& rhs) const;
	bool operator!=(const Address& rhs) const;
};




//IP地址的基类
class IPAddress : public Address {
public:
	typedef std::shared_ptr<IPAddress> ptr;

	/**
	 * @brief 通过域名,IP,服务器名创建IPAddress
	 * @param[in] address 域名,IP,服务器名等.举例: www.sylar.top
	 * @param[in] port 端口号
	 * @return 调用成功返回IPAddress,失败返回nullptr
	 */
	static IPAddress::ptr Create(const char* address, uint16_t port = 0);

	/**
	 * @brief 获取该地址的广播地址
	 * @param[in] prefix_len 子网掩码位数
	 * @return 调用成功返回IPAddress,失败返回nullptr
	 */
	virtual IPAddress::ptr broadcastAddress(uint32_t prefix_len) = 0;

	/**
	 * @brief 获取该地址的网段
	 * @param[in] prefix_len 子网掩码位数
	 * @return 调用成功返回IPAddress,失败返回nullptr
	 */
	virtual IPAddress::ptr networkAddress(uint32_t prefix_len) = 0;

	/**
	 * @brief 获取子网掩码地址
	 * @param[in] prefix_len 子网掩码位数
	 * @return 调用成功返回IPAddress,失败返回nullptr
	 */
	virtual IPAddress::ptr subnetMask(uint32_t  prefix_len) = 0;

	//返回端口号
	virtual uint32_t getPort() const = 0;
	//设置端口号
	virtual void setPort(uint16_t v) = 0;


};


//IPv4地址
class IPv4Address : public IPAddress {
public:
	typedef std::shared_ptr<IPv4Address> ptr;

	//使用点分十进制地址创建IPv4Address
	//address：点分十进制地址如192.178.3.4
	//port：端口号
	static IPv4Address::ptr Create(const char* address, uint16_t port = 0);

    //通过sockaddr_in构造IPvAddress
    IPv4Address(const sockaddr_in& address);
	//通过二进制地址构造IPv4Address
	IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);

	const sockaddr* getAddr() const override;
	sockaddr* getAddr() override;
	socklen_t getAddrLen() const override;
	std::ostream& insert(std::ostream& os) const override;

	IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
	IPAddress::ptr networkAddress(uint32_t prefix_len) override;
	IPAddress::ptr subnetMask(uint32_t prefix_len) override;
	uint32_t getPort() const override;
	void setPort(uint16_t v) override;

private:
	sockaddr_in m_addr;
};

//IPv6地址
class IPv6Address : public IPAddress {
public:
	typedef std::shared_ptr<IPv6Address> ptr;

	/**
	 * @brief 通过IPv6地址字符串构造IPv6Address
	 * @param[in] address IPv6地址字符串
	 * @param[in] port 端口号
	 */
	static IPv6Address::ptr Create(const char* address, uint16_t port = 0);

	IPv6Address();
    IPv6Address(const sockaddr_in6& address);
	IPv6Address(const uint8_t address[16], uint16_t port);

	const sockaddr* getAddr() const override;
	sockaddr* getAddr() override;
	socklen_t getAddrLen() const override;
	std::ostream& insert(std::ostream& os) const override;

	IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
	IPAddress::ptr networkAddress(uint32_t prefix_len) override;
	IPAddress::ptr subnetMask(uint32_t prefix_len) override;
	uint32_t getPort() const override;
	void setPort(uint16_t v) override;

private:
	sockaddr_in6 m_addr;
};


//UnixSocket地址
//在 TCP/IP 网络通信 中，socket 使用 IP 地址 + 端口 进行寻址，而 Unix Socket 不走网络协议，而是直接在文件系统里创建一个特殊的文件作为通信端点。因此，它的“地址”就是文件系统中的一个路径，比如：
//  /tmp/my_socket
//  /var/run/docker.sock
//  /run/systemd/journal/socket
//当两个进程要通信时，它们会连接到相同的 Unix Socket 文件，然后像读写普通文件一样交换数据
//对于客户端服务端来说，服务端会创建一个.sock文件，客户端会连接这个文件，实现客户端与服务端的通信
class UnixAddress : public Address {
public:
	typedef std::shared_ptr<UnixAddress> ptr;

	/**
	 * @brief 无参构造函数
	 */
	UnixAddress();

	/**
	 * @brief 通过路径构造UnixAddress
	 * @param[in] path UnixSocket路径(长度小于UNIX_PATH_MAX)
	 */
	UnixAddress(const std::string& path);

	const sockaddr* getAddr() const override;
	sockaddr* getAddr() override;
	socklen_t getAddrLen() const override;
	void setAddrLen(uint32_t v);
	std::string getPath() const;
	std::ostream& insert(std::ostream& os) const override;
private:
	sockaddr_un m_addr;
	//存储当前unix域套接字地址的有效长度，这个长度在套接字api中很重要
	//因为bind()connect()sendto()等系统的调用需要传递结构体的大小
	//而sockaddr_un结构体的大小不是固定的，他的实际使用长度取决于sun_path中存储的路径长度
	//具体来说比如/tmp/sock这个路径只需要10个字节，则sockaddr_un中sun_path后面的字节是无效的，
	//不应该被bind之类的系统调用使用，此时m_length的作用就体现出来了
	socklen_t m_length;
};


//未知地址
//UnknownAddress（未知地址）类的存在主要是为了处理不支持的地址类型
//或未知的套接字地址，以确保代码的健壮性和扩展性。
class UnknownAddress : public Address {
public:
	typedef std::shared_ptr<UnknownAddress> ptr;

	UnknownAddress(int family);
	UnknownAddress(const sockaddr& addr);
	const sockaddr* getAddr() const override;
	sockaddr* getAddr() override;
	socklen_t getAddrLen() const override;
	std::ostream& insert(std::ostream& os) const override;

private:
	sockaddr m_addr;
};



}

#endif