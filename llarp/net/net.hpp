#ifndef LLARP_NET_HPP
#define LLARP_NET_HPP

#include <net/address_info.hpp>
#include <net/net_int.hpp>
#include <net/net.h>
#include <util/logger.hpp>
#include <util/mem.hpp>
#include <util/string_view.hpp>
#include <util/bits.hpp>

#include <functional>
#include <cstdlib>  // for itoa
#include <vector>

// for addrinfo
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#define inet_aton(x, y) inet_pton(AF_INET, x, y)
#endif

bool
operator==(const sockaddr& a, const sockaddr& b);

bool
operator==(const sockaddr_in& a, const sockaddr_in& b);

bool
operator==(const sockaddr_in6& a, const sockaddr_in6& b);

bool
operator<(const sockaddr_in6& a, const sockaddr_in6& b);

bool
operator<(const in6_addr& a, const in6_addr& b);

bool
operator==(const in6_addr& a, const in6_addr& b);

struct privatesInUse
{
  // true if used by real NICs on start
  // false if not used, and means we could potentially use it if needed
  bool ten;       // 16m ips
  bool oneSeven;  // 1m  ips
  bool oneNine;   // 65k ips
};

struct privatesInUse
llarp_getPrivateIfs();

namespace llarp
{
  struct IPRange
  {
    huint32_t addr;
    huint32_t netmask_bits;

    /// return true if ip is contained in this ip range
    bool
    Contains(const huint32_t& ip) const
    {
      return (addr & netmask_bits) == (ip & netmask_bits);
    }

    friend std::ostream&
    operator<<(std::ostream& out, const IPRange& a)
    {
      return out << a.ToString();
    }

    std::string
    ToString() const
    {
      return addr.ToString() + "/"
          + std::to_string(llarp::bits::count_bits(netmask_bits.h));
    }
  };

  /// get a netmask with the higest numset bits set
  constexpr uint32_t
  __netmask_ipv4_bits(uint32_t numset)
  {
    return (32 - numset) != 0u ? (1 << numset) | __netmask_ipv4_bits(numset + 1) : 0;
  }

  /// get an ipv4 netmask given some /N range
  constexpr huint32_t
  netmask_ipv4_bits(uint32_t num)
  {
    return huint32_t{__netmask_ipv4_bits(32 - num)};
  }

  constexpr huint32_t
  ipaddr_ipv4_bits(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
  {
#if __BYTE_ORDER == __ORDER_BIG_ENDIAN__
    return huint32_t{(a) | (b << 8) | (c << 16) | (d << 24)};
#else
    return huint32_t{(d) | (c << 8) | (b << 16) | (a << 24)};
#endif
  }

  constexpr IPRange
  iprange_ipv4(byte_t a, byte_t b, byte_t c, byte_t d, byte_t mask)
  {
    return IPRange{ipaddr_ipv4_bits(a, b, c, d), netmask_ipv4_bits(mask)};
  }

  bool
  IsIPv4Bogon(const huint32_t& addr);

  constexpr bool
  ipv6_is_siit(const in6_addr& addr)
  {
    return addr.s6_addr[11] == 0xff && addr.s6_addr[10] == 0xff
        && addr.s6_addr[9] == 0 && addr.s6_addr[8] == 0 && addr.s6_addr[7] == 0
        && addr.s6_addr[6] == 0 && addr.s6_addr[5] == 0 && addr.s6_addr[4] == 0
        && addr.s6_addr[3] == 0 && addr.s6_addr[2] == 0 && addr.s6_addr[1] == 0
        && addr.s6_addr[0] == 0;
  }

  bool
  IsBogon(const in6_addr& addr);

  bool
  IsBogonRange(const in6_addr& host, const in6_addr& mask);

  struct Addr;  // fwd declr

  bool
  AllInterfaces(int af, Addr& addr);

  /// get first network interface with public address
  bool
  GetBestNetIF(std::string& ifname, int af = AF_INET);

  /// look at adapter ranges and find a free one
  std::string
  findFreePrivateRange();

  /// look at adapter names and find a free one
  std::string
  findFreeLokiTunIfName();

  /// get network interface address for network interface with ifname
  bool
  GetIFAddr(const std::string& ifname, Addr& addr, int af = AF_INET);

}  // namespace llarp

#include <net/net_addr.hpp>
#include <net/net_inaddr.hpp>

#endif
