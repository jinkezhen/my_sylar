

#include "address.h"
#include "endian.h"
#include "log.h"





#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

//该函数用于生成一个掩码，用于保留T类型数据的低bits位，并清除其余位，如果bits=8，而T是uint32_t，则返回的掩码是0x000000FF（即低 8 位是 1，其它位是 0）
template<class T>
static T CreateMask(uint32_t bits) {
	return (1 << (sizeof(T) * 8 - bits)) - 1;
}

//该函数用于统计一个整数的二进制表示中有多少个1，比如value=5(101)，返回2，因为有两个1
template<class T>
static uint32_t CountBytes(T value) {
	uint32_t result = 0;
	for (; value > 0; result++) {
		value &= value - 1;
	}
	return result;
}

Address::ptr Address::LookupAny(const std::string& host, int family, int type, int protocol) {
	std::vector<Address::ptr> result;
	if (Lookup(result, host, family, type, protocol)) {
		return result[0];
	}
	return nullptr;
}

IPAddress::ptr Address::LookupAnyIPAddress(const std::string& host, int family, int type, int protocol) {
	std::vector<Address::ptr> result;
	if (Lookup(result, host, family, type, protocol)) {
		for (auto& i : result) {
			IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
			if (v) {
				return v;
			}
		}
	}
	return nullptr;
}

//addrinfo结构体
//struct addrinfo {
//	int ai_flags;       // 特殊选项，比如 AI_PASSIVE
//	int ai_family;      // 协议族，如 AF_INET（IPv4）、AF_INET6（IPv6）
//	int ai_socktype;    // 套接字类型，如 SOCK_STREAM（TCP）、SOCK_DGRAM（UDP）
//	int ai_protocol;    // 协议，如 IPPROTO_TCP、IPPROTO_UDP
//	size_t ai_addrlen;  // 结构体 ai_addr 的长度
//	char* ai_canonname; // 主机的规范名称
//	struct sockaddr* ai_addr;  // 地址结构体
//	struct addrinfo* ai_next;  // 下一个 addrinfo 结构体
//};
bool Address::Lookup(std::vector<Address::ptr>& result, const std::string& host, int family, int type, int protocol) {
	addrinfo hints;    //用于getaddrinfo()的传参，来告诉系统需要什么类型的地址信息
	addrinfo* results; //getaddrinfo()调用后，会返回一个addrinfo结构的链表，指向解析得到的地址
	addrinfo* next;    //用于遍历results链表

	//初始化hints结构体
	//addrinfo hints = {}; //使用{}初始化，可以避免所有的字段都被初始化为0了
	//hints.ai_flags = AI_PASSIVE;
	//hints.ai_family = family;
	//hints.ai_socktype = type;
	//hints.ai_protocol = protocol;
	hints.ai_flags = 0;
	hints.ai_family = family;
	hints.ai_socktype = type;
	hints.ai_protocol = protocol;
	hints.ai_addrlen = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	//node存储提取后的主机名/ip地址
	//service指向端口号字符串（如果存在端口号）
	std::string node;
	const char* service = NULL;

	//检查ipv6 address service
	//该分支适用于解析[2001:db8::1]	[2001:db8::1]:80
	if (!host.empty() && host[0] == '[') {
		const char* endipv6 = (const char*)memchr(host.c_str() + 1, ']', host.size() - 1);
		if (endipv6) {
			if (*(endipv6 + 1) == ':') {
				service = endipv6 + 2;
			}
			node = host.substr(1, endipv6 - host.c_str() - 1);
		}
	}

	//该分支适用于解析192.168.1.1:8080	example.com:80 : 8080
	if (node.empty()) {
		service = (const char*)memchr(host.c_str(), ':', host.size());
		if (service) {
			//检查是否有第二个:如果没有的话就将service后面的部分作为端口号
			if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
				node = host.substr(0, service - host.c_str());
				++service;
			}
		}
	}

	if (node.empty()) node = host;

	//getaddrinfo是一个标准的网络库函数，用于解析主机名和服务名，返回与之匹配的套接字地址信息
	//hints包含查询时的一些选项
	//results保存匹配的地址信息链
	int error = getaddrinfo(node.c_str(), service, &hints, &results);
	if (error) {
		SYLAR_LOG_DEBUG(g_logger) << "Address::Lookup getaddress(" << host << ", "
			<< family << ", " << type << ") err=" << error << " errstr="
			<< gai_strerror(error);
		return false;
	}

	next = results;
	while (next) {
		//使用Create将ai_addr和ai_addrlen转换为Address::ptr类型
		result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
		next = next->ai_next;
	}

	freeaddrinfo(results);
	return !result.empty();
}

//struct ifaddrs {
//	struct ifaddrs* ifa_next;    // 指向下一个网络接口
//	char* ifa_name;    // 接口名称 (如 "eth0", "lo")
//	unsigned int     ifa_flags;   // 标志，表示是否启用等
//	struct sockaddr* ifa_addr;    // 该接口的 IP 地址
//	struct sockaddr* ifa_netmask; // 该接口的子网掩码
//};
bool Address::GetInterfaceAddresses(std::multimap<std::string
	, std::pair<Address::ptr, uint32_t> >& result,
	int family) {
	struct ifaddrs* next, * results;
	//getifaddrs()是一个系统调用，获取本机的所有网络接口信息，并返回ifaddrs结构体链表的头指针
	/*假设本机有两个网络接口：
		lo（回环接口，IPv4）
		eth0（以太网接口，IPv4 和 IPv6）

		那么 results 链表可能是这样的：
		results →[lo, AF_INET, 127.0.0.1, 255.0.0.0]
		→[eth0, AF_INET, 192.168.1.100, 255.255.255.0]
		→[eth0, AF_INET6, fe80::1, ffff:ffff:fff]*/
	if (getifaddrs(&results) != 0) {
		SYLAR_LOG_DEBUG(g_logger) << "Address::GetInterfaceAddresses getifaddrs "
			" err=" << errno << " errstr=" << strerror(errno);
		return false;
	}
	try {
		for (next = results; next != nullptr; next = next->ifa_next) {
			Address::ptr addr;  //存储当前接口的IP地址对象
			uint32_t prefix_len = ~0u; //~0u(全1)存储子网掩码的前缀长度

			//过滤family类型
			//AF_UNSPEC代表不限制ip地址类型，获取所有地址
			if (family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
				continue;
			}
			
			switch (next->ifa_addr->sa_family) {
				case AF_INET: {
					//创建ipv4地址对象
					addr = Create(next->ifa_addr, sizeof(sockaddr_in));
					//ifa_next是一个通用结构体，不同地址类型的子网掩码存储方式不同，还需要将其转换为IPv4或IPv6对应的sockaddr_in sockaddr_in6
					//struct sockaddr_in {
					//	sa_family_t sin_family;  // 地址族 (AF_INET)
					//	in_port_t sin_port;      // 端口号
					//	struct in_addr sin_addr; // IP 地址 (网络字节序)
					//};
					//struct in_addr {
					//	uint32_t s_addr; // 32 位 IPv4 地址
					//};
					uint32_t netmask = ((sockaddr_in*)next->ifa_netmask)->sin_addr.s_addr;
					//计算掩码中1的个数
					prefix_len = CountBytes(netmask);
					}
					break;

				case AF_INET6: {
					//struct sockaddr_in6 {
					//	sa_family_t     sin6_family;   // 地址族 (AF_INET6)
					//	in_port_t       sin6_port;     // 端口号 (通常为 0)
					//	uint32_t        sin6_flowinfo; // 流信息 (通常为 0)
					//	struct in6_addr sin6_addr;     // IPv6 地址
					//	uint32_t        sin6_scope_id; // 作用域 ID (常用于 link-local 地址)
					//};
					//struct in6_addr {
					//	uint8_t s6_addr[16]; // 128 位 IPv6 地址（16 字节）
					//};
					addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
					in6_addr& netmask = ((sockaddr_in6*)next->ifa_netmask)->sin6_addr;
					prefix_len = 0;
					for (int i = 0; i < 16; ++i) {
						/*假设 ifa_netmask 里的值是 ffff : ffff:ffff:ffff::（即 / 64 子网掩码），那么：
							netmask.s6_addr = { 0xFF, 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00, 0x00,
												0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00 };*/
						prefix_len += CountBytes(netmask.s6_addr[i]);
					}
					}
					break;
				default:
					break;
			}
			if (addr) {
				result.insert(std::make_pair(next->ifa_name, std::make_pair(addr, prefix_len)));
			}


		}
	}
	catch (...) {
		SYLAR_LOG_ERROR(g_logger) << "Address::GetInterfaceAddresses exception";
		freeifaddrs(results);
		return false;
	}
	freeifaddrs(results);
	return !result.empty();
}

bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t> >& result
	, const std::string& iface, int family) {
	if (iface.empty() || iface == "*") {
		if (family == AF_INET || family == AF_UNSPEC) {
			result.push_back(std::make_pair(std::make_shared<IPv4Address>(), 0u));
		}
		if (family == AF_INET6 || family == AF_UNSPEC) {
			result.push_back(std::make_pair(std::make_shared<IPv6Address>(), 0u));
		}
		return true;
	}
	std::multimap<std::string, std::pair<Address::ptr, uint32_t>> results;
	if (!GetInterfaceAddresses(results, family)) {
		return false;
	}
	//multimap的equal_range(key)方法，用于获取容器内所有的键为key的键值对，并且返回一个std::pair<iterator, iterator>
	//其中返回的pair.first指向容器中第一个匹配的键值对，pair.second指向最后一个匹配的键值对之后的一个键值对
	auto its = results.equal_range(iface);
	for (; its.first != its.second; ++its.first) {
		result.push_back(its.first->second);
	}
	return !result.empty();
}

int Address::getFamily() const {
	return getAddr()->sa_family;
}

std::string Address::toString() const {
	std::stringstream ss;
	insert(ss);
	return ss.str();
}

//这个create函数用于根据传入的sockaddr指针，创建并返回相应类型的Address智能指针，支持IPv4、IPv6以及未知地址类型
//struct sockaddr {
//	sa_family_t sa_family;  // 地址族（AF_INET、AF_INET6等）
//	char sa_data[14];       // 地址数据，存储IP地址和端口信息
//};
Address::ptr Address::Create(const sockaddr* addr, socklen_t addrlen) {
	if (addr == nullptr) {
		return nullptr;
	}
	Address::ptr result;
	switch (addr->sa_family) {
		case AF_INET:
			result.reset(new IPv4Address(*(const sockaddr_in*)addr));
			break;
		case AF_INET6:
			result.reset(new IPv6Address(*(const sockaddr_in6*)addr));
			break;
		default:
			result.reset(new UnknownAddress(*addr));
			break;
	}
	return result;
}

//实际比较的是地址
bool Address::operator<(const Address& rhs) const {
	socklen_t minLen = std::min(this->getAddrLen(), rhs.getAddrLen());
	//getAddr返回的是sockaddr*指针，即地址的二进制数据
	int result = memcmp(this->getAddr(), rhs.getAddr(), minLen);
	if (result < 0) {
		return true;   //*this<rhs
	}
	else if (result > 0) {
		return false;  //*this>rhs
	}
	else if (this->getAddrLen() < rhs.getAddrLen()) {
		return true;
	}
	else {
		return false;
	}
}

bool Address::operator==(const Address& rhs) const {
	return this->getAddrLen() == rhs.getAddrLen()
		&& memcmp(this->getAddr(), rhs.getAddr(), this->getAddrLen()) == 0;
}

bool Address::operator!=(const Address& rhs) const {
	return !(*this == rhs);
}



//IPvAddress
//根据传入的IP地址和端口号创建一个IPAddress对象
IPAddress::ptr IPAddress::Create(const char* address, uint16_t port) {
	addrinfo hints, * results;
	memset(&hints, 0, sizeof(addrinfo));

	hints.ai_flags = AI_NUMERICHOST;  //这个标志告诉getaddrinfo(),address必须是一个数值IP地址，而不能是域名，如果不加这个标志，getaddrinfo()可能会尝试DNS解析，把域名转换为IP，这样做的目的是IPAddress仅用于IP地址解析，而不是域名解析，所以需要强制address只能是IP地址
	hints.ai_family = AF_UNSPEC;  //不限制具体某种协议族

	int error = getaddrinfo(address, NULL, &hints, &results);
	if (error) {
		SYLAR_LOG_DEBUG(g_logger) << "IPAddress::Create(" << address
			<< ", " << port << ") error=" << error
			<< " errno=" << errno << " errstr=" << strerror(errno);
		return nullptr;
	}

	try {
		IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
		if (result) {
			result->setPort(port);
		}
		freeaddrinfo(results);
		return result;
	}
	catch (...) {
		freeaddrinfo(results);
		return nullptr;
	}
}


//IPv4Address
IPv4Address::ptr IPv4Address::Create(const char* address, uint16_t port) {
	IPv4Address::ptr rt(new IPv4Address());
	//sockaddr_in结构的sin_port需要以大端网络字节序存储，而port通常是主机字节序(小端)，所以需要转换成大端字节序
	rt->m_addr.sin_port = byteswapOnBigEndian(port);
	//inet_pton用于将点分十进制字符串形式的IPv4地址转换为二进制形式，存储在sin_addr中
	//sin_addr专门用于存储IPv4地址的二进制格式
	int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
	if (result <= 0) {
		return nullptr;
	}
	return rt;
}

IPv4Address::IPv4Address(const sockaddr_in& address) {
	m_addr = address;
}

IPv4Address::IPv4Address(uint32_t address, uint16_t port) {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sin_family = AF_INET;
	m_addr.sin_port = byteswapOnBigEndian(port);
	m_addr.sin_addr.s_addr = byteswapOnBigEndian(address);
}

const sockaddr* IPv4Address::getAddr() const {
	return (sockaddr*)&m_addr;
}

socklen_t IPv4Address::getAddrLen() const {
	return sizeof(m_addr);
}

//将IPv4Address对象以字符串格式输出到ostream，即准换为标准的 点分十进制:端口号 的格式192.178.2.3:8080
std::ostream& IPv4Address::insert(std::ostream& os) const {
	uint32_t addr = byteswapOnBigEndian(m_addr.sin_addr.s_addr);
	os << ((addr >> 24) & 0xff) << "." 
	   << ((addr >> 16) & 0xff) << "." 
	   << ((addr >> 8) & 0xff)  << "."
	   << ((addr & 0xff));
	os << ":" << byteswapOnBigEndian(m_addr.sin_port);
	return os;
}

//广播地址：某个网络中向所有主机发送数据包的特殊IP地址
//计算方法：先计算子网掩码，他用于区分网络位和主机位，将主机位全部设置为1，得到广播地址
//prefix_len：子网前缀长度
IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len) {
	//子网掩码长度最大是32位
	if (prefix_len > 32) {
		return nullptr;
	}
	sockaddr_in baddr(m_addr);
	baddr.sin_addr.s_addr |= byteswapOnBigEndian(CreateMask<uint32_t>(prefix_len));
	return IPv4Address::ptr(new IPv4Address(baddr));
}

//网络地址：某个子网的起始IP地址，主机部分全为0
//计算方法：当前IP的地址&子网掩码
IPAddress::ptr IPv4Address::networkAddress(uint32_t prefix_len) {
	if (prefix_len > 32) {
		return nullptr;
	}
	sockaddr_in naddr(m_addr);
	naddr.sin_addr.s_addr &= byteswapOnBigEndian(CreateMask<uint32_t>(prefix_len));
	return IPv4Address::ptr(new IPv4Address(naddr));
} 

//子网掩码：用于区分网络部分和主机部分
//计算方法：前prefix_len位为1，剩下的部分填充0
IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len) {
	sockaddr_in subnetMask;
	memset(&subnetMask, 0, sizeof(subnetMask));
	subnetMask.sin_addr.s_addr = ~(byteswapOnBigEndian(CreateMask<uint32_t>(prefix_len)));
	subnetMask.sin_family = AF_INET;
	return IPv4Address::ptr(new IPv4Address(subnetMask));
}

//端口号是用于区分不同网络服务的标识，范围是0-65535
//sin_port存储的是网络字节序，需要转为主机字节序
uint32_t IPv4Address::getPort() const {
	return byteswapOnBigEndian(m_addr.sin_port);
}

void IPv4Address::setPort(uint16_t v) {
	m_addr.sin_port = byteswapOnBigEndian(v);
}



//IPv6Address
IPv6Address::ptr IPv6Address::Create(const char* address, uint16_t port) {
	IPv6Address::ptr rt(new IPv6Address());
	rt->m_addr.sin6_port = byteswapOnBigEndian(port);
	//将字符串转为IPv6地址
	int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
	if (result <= 0) {
		return nullptr;
	}
	return rt;
}

IPv6Address::IPv6Address() {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sin6_family = AF_INET6;
}

IPv6Address::IPv6Address(const sockaddr_in6& address) {
	m_addr = address;
}

IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port) {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sin6_family = AF_INET6;
	m_addr.sin6_port = byteswapOnBigEndian(port);
	//拷贝ipv6地址
	memcpy(&m_addr.sin6_addr, address, 16);
}

const sockaddr* IPv6Address::getAddr() const {
	return (sockaddr*)&m_addr;
}

socklen_t IPv6Address::getAddrLen() const {
	return sizeof(m_addr);
}

std::ostream& IPv6Address::insert(std::ostream& os) const {
	os << "[";
	//s6_addr存储一个16字节的IPv6地址
	//这里将其强转为uint16_t*是因为IPv6地址通常以8组16位(2字节)的hex的形式表示，此时addr[i]表示第i组的16位的数值
	//2001:8scd:0sdc:8233:0xd2:scds:00d9:0dsi
	//
	//原本的s6_addr是一个uint8_t s6_addr[16]的形式
	//uint8_t s6_addr[16] = {
	//	0x20, 0x01, 0x0d, 0xb8, 
	//	0x85, 0xa3, 0x00, 0x00, 
	//	0x00, 0x00, 0x8a, 0x2e,
	//	0x03, 0x70, 0x73, 0x34 
	//};
	//通过(uint16_t*)强转为 { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xffff, 0xc0a8, 0x0101 }该种形式，正好对应IPv6的地址表示
	uint16_t* addr = (uint16_t*)m_addr.sin6_addr.s6_addr;
	//标记是否使用了::省略连续的0,在ipv6地址中，如果有连续的0可以用::代替，但是::只能使用一次
	bool used_zero = false;
	for (size_t i = 0; i < 8; ++i) {
		//如果当前是0，并且没有使用过::
		if (addr[i] == 0 && !used_zero) {
			continue;
		}
		if (i && addr[i - 1] == 0 && !used_zero) {
			os << ":";
			used_zero = true;
		}
		if (i) {
			os << ":";
		}
		//std::dec让后续输出恢复为10进制
		os << std::hex << (int)byteswapOnBigEndian(addr[i]) << std::dec;
	}
	if (!used_zero && addr[7] == 0) {
		os << "::";
	}
	os << "]" << byteswapOnBigEndian(m_addr.sin6_port);
	return os;
}

//几个地址的计算逻辑：以网络地址为例
//1.假设 prefix_len = 55，表示 前 55 位是网络部分，剩下的是主机部分。
//比如一个16进制地址，2001:0db8 : abcd : 1234 : 5678 : 9abc : def0 : 1234
//prefix_len / 8 = 6计算prefix_len所在的字节索引(即网络地址涉及到的最后一个字节)。
//prefix_len % 8 = 7计算prefix_len在该字节中的比特位置(即网络地址的最后一个字节中的最后一位)
//2.CreateMask<uint8_t>(prefix_len % 8) 返回的是临界字节需要的掩码，在该情况下为CreateMask<uint8_t>(7) = 0b00000001  // (1 << (8 - 7)) - 1 = 0b00000001
//3.baddr.sin6_addr.s6_addr[6] |= 0b00000001;的作用是将网络地址在ipv6中的临界字节(网络地址的最后一个字节中不属于网络地址的那一位设置为1)
//4.for (int i = prefix_len / 8 + 1; i < 16; ++i) {baddr.sin6_addr.s6_addr[i] = 0xff;}这个循环的作用是将ipv6网络地址最后一个字节之后的所有字节都置为1
IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len) {
	sockaddr_in6 baddr(m_addr);
	baddr.sin6_addr.s6_addr[prefix_len / 8] |= CreateMask<uint8_t>(prefix_len % 8);
	for (int i = prefix_len / 8 + 1; i < 16; ++i) {
		baddr.sin6_addr.s6_addr[i] = 0xff;
	}
	return IPv6Address::ptr(new IPv6Address(baddr));
}

IPAddress::ptr IPv6Address::networkAddress(uint32_t prefix_len) {
	sockaddr_in6 baddr(m_addr);
	baddr.sin6_addr.s6_addr[prefix_len / 8] &= CreateMask<uint8_t>(prefix_len % 8);
	for (int i = prefix_len / 8 + 1; i < 16; ++i) {
		baddr.sin6_addr.s6_addr[i] = 0x00;
	}
	return IPv6Address::ptr(new IPv6Address(baddr));
}

IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len) {
	sockaddr_in6 subnet;
	memset(&subnet, 0, sizeof(subnet));
	subnet.sin6_addr.s6_addr[prefix_len / 8] = ~CreateMask<uint8_t>(prefix_len % 8);
	for (uint32_t i = 0; i < prefix_len / 8; ++i) {
		subnet.sin6_addr.s6_addr[i] = 0xff;
	}
	return IPv6Address::ptr(new IPv6Address(subnet));
}

uint32_t IPv6Address::getPort() const {
	return byteswapOnBigEndian(m_addr.sin6_port);
}

void IPv6Address::setPort(uint16_t v) {
	m_addr.sin6_port = byteswapOnBigEndian(v);
}


//UnixAddress
// struct sockaddr_un {
// 	sa_family_t sun_family; //地址族，必须是AF_UNIX
// 	char sun_path[108];     //unix套接字路径
// };

//offsetof宏
//<stddef.h中>
// #define offset(type, member) ((size_t)&((type*)0)->member)
//作用：计算type的某个member在type结构体中的字节偏移量，即member距离type结构体起始地址的字节数


//-1是因为sun_path需要一个字节留给\0终止符
static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

UnixAddress::UnixAddress() {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sun_family = AF_UNIX;
	m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixAddress::UnixAddress(const std::string& path) {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sun_family = AF_UNIX;
	m_length = path.size() + 1;  //+1是因为还要包括'\0'
	//如果path以\0开头，表示使用抽象Unix套接字(不会在文件系统中创建文件)，此时就算长度为1，也不要这个长度，因为终止符无意义
	if (!path.empty() && path[0] == '\0') {
		--m_length;
	}
	if (m_length > sizeof(m_addr.sun_path)) {
		throw std::logic_error("path too long");
	}
	memcpy(&m_addr.sun_path, path.c_str(), m_length);
	m_length += offsetof(sockaddr_un, sun_path);
}

void UnixAddress::setAddrLen(uint32_t v) {
	m_length = v;
}

sockaddr* UnixAddress::getAddr() {
	return (sockaddr*)&m_addr;
}

const sockaddr* UnixAddress::getAddr() const {
	return (sockaddr*)&m_addr;
}

socklen_t UnixAddress::getAddrLen() const {
	return m_length;
}

std::string UnixAddress::getPath() const {
	std::stringstream ss;
	ss << m_addr.sun_path;
	return ss.str();
}

std::ostream& UnixAddress::insert(std::ostream& os) const {
	return os << m_addr.sun_path;
}

//UnknownAddress
UnknownAddress::UnknownAddress(int family) {
	memset(&m_addr, 0, sizeof(m_addr));
	m_addr.sa_family = family;
}

UnknownAddress::UnknownAddress(const sockaddr& addr) {
	m_addr = addr;
}

sockaddr* UnknownAddress::getAddr() {
	return (sockaddr*)&m_addr;
}

const sockaddr* UnknownAddress::getAddr() const {
	return (sockaddr*)&m_addr;
}

socklen_t UnknownAddress::getAddrLen() const {
	return sizeof(m_addr);
}

std::ostream& UnknownAddress::insert(std::ostream& os) const {
	return os << m_addr.sa_family;
}

std::ostream& operator<<(std::ostream& os, const Address& addr) {
	return addr.insert(os);
}


}