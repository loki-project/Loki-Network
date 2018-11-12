#include "llarp/net.hpp"
#include "str.hpp"
#ifdef ANDROID
#include "android/ifaddrs.h"
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#ifndef ANDROID
#include <ifaddrs.h>
#endif
#include <net/if.h>
#endif
#include <cstdio>
#include "logger.hpp"

//#include <llarp/net_inaddr.hpp>
#include <llarp/net_addr.hpp>

bool
operator==(const sockaddr& a, const sockaddr& b)
{
  if(a.sa_family != b.sa_family)
    return false;
  switch(a.sa_family)
  {
    case AF_INET:
      return *((const sockaddr_in*)&a) == *((const sockaddr_in*)&b);
    case AF_INET6:
      return *((const sockaddr_in6*)&a) == *((const sockaddr_in6*)&b);
    default:
      return false;
  }
}

bool
operator<(const sockaddr_in6& a, const sockaddr_in6& b)
{
  return memcmp(&a, &b, sizeof(sockaddr_in6)) < 0;
}

bool
operator<(const in6_addr& a, const in6_addr& b)
{
  return memcmp(&a, &b, sizeof(in6_addr)) < 0;
}

bool
operator==(const in6_addr& a, const in6_addr& b)
{
  return memcmp(&a, &b, sizeof(in6_addr)) == 0;
}

bool
operator==(const sockaddr_in& a, const sockaddr_in& b)
{
  return a.sin_port == b.sin_port && a.sin_addr.s_addr == b.sin_addr.s_addr;
}

bool
operator==(const sockaddr_in6& a, const sockaddr_in6& b)
{
  return a.sin6_port == b.sin6_port && a.sin6_addr == b.sin6_addr;
}

#ifdef _WIN32
#include <assert.h>
#include <errno.h>
#include <iphlpapi.h>
#include <strsafe.h>

#define DEFAULT_BUFFER_SIZE 15000

struct llarp_nt_ifaddrs_t
{
  struct llarp_nt_ifaddrs_t* ifa_next; /* Pointer to the next structure.  */
  char* ifa_name;                      /* Name of this network interface.  */
  unsigned int ifa_flags;              /* Flags as from SIOCGIFFLAGS ioctl.  */
  struct sockaddr* ifa_addr;           /* Network address of this interface.  */
  struct sockaddr* ifa_netmask;        /* Netmask of this interface.  */
};

// internal struct
struct _llarp_nt_ifaddrs_t
{
  struct llarp_nt_ifaddrs_t _ifa;
  char _name[256];
  struct sockaddr_storage _addr;
  struct sockaddr_storage _netmask;
};

static inline void*
_llarp_nt_heap_alloc(const size_t n_bytes)
{
  /* Does not appear very safe with re-entrant calls on XP */
  return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, n_bytes);
}

static inline void
_llarp_nt_heap_free(void* mem)
{
  HeapFree(GetProcessHeap(), 0, mem);
}
#define llarp_nt_new0(struct_type, n_structs) \
  ((struct_type*)malloc((size_t)sizeof(struct_type) * (size_t)(n_structs)))

int
llarp_nt_sockaddr_pton(const char* src, struct sockaddr* dst)
{
  struct addrinfo hints;
  struct addrinfo* result = nullptr;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = AI_NUMERICHOST;
  const int status  = getaddrinfo(src, nullptr, &hints, &result);
  if(!status)
  {
    memcpy(dst, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    return 1;
  }
  return 0;
}

/* Supports both IPv4 and IPv6 addressing.  The size of IP_ADAPTER_ADDRESSES
 * changes between Windows XP, XP SP1, and Vista with additional members.
 *
 * Interfaces that are not "operationally up" will typically return a host
 * IP address with the defined IPv4 link-local prefix 169.254.0.0/16.
 * Adapters with a static configured IP address but down will return both
 * the IPv4 link-local prefix and the static address.
 *
 * It is easier to say "not up" rather than "down" as this API returns six
 * effective down status values: down, testing, unknown, dormant, not present,
 * and lower layer down.
 *
 * Available in Windows XP and Wine 1.3.
 */
static bool
_llarp_nt_getadaptersaddresses(struct llarp_nt_ifaddrs_t** ifap)
{
  DWORD dwSize                            = DEFAULT_BUFFER_SIZE, dwRet;
  IP_ADAPTER_ADDRESSES *pAdapterAddresses = nullptr, *adapter;

  /* loop to handle interfaces coming online causing a buffer overflow
   * between first call to list buffer length and second call to enumerate.
   */
  for(unsigned i = 3; i; i--)
  {
#ifdef DEBUG
    fprintf(stderr, "IP_ADAPTER_ADDRESSES buffer length %lu bytes.\n", dwSize);
#endif
    pAdapterAddresses = (IP_ADAPTER_ADDRESSES*)_llarp_nt_heap_alloc(dwSize);
    dwRet = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST
            | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME
            | GAA_FLAG_SKIP_MULTICAST,
        nullptr, pAdapterAddresses, &dwSize);
    if(ERROR_BUFFER_OVERFLOW == dwRet)
    {
      _llarp_nt_heap_free(pAdapterAddresses);
      pAdapterAddresses = nullptr;
    }
    else
    {
      break;
    }
  }

  switch(dwRet)
  {
    case ERROR_SUCCESS:
      break;
    case ERROR_BUFFER_OVERFLOW:
      errno = ENOBUFS;
      if(pAdapterAddresses)
        _llarp_nt_heap_free(pAdapterAddresses);
      return FALSE;
    default:
      errno = _doserrno;
      if(pAdapterAddresses)
        _llarp_nt_heap_free(pAdapterAddresses);
      return FALSE;
  }

  /* count valid adapters */
  int n = 0, k = 0;
  for(adapter = pAdapterAddresses; adapter; adapter = adapter->Next)
  {
    for(IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
        unicast; unicast                    = unicast->Next)
    {
      /* ensure IP adapter */
      if(AF_INET != unicast->Address.lpSockaddr->sa_family
         && AF_INET6 != unicast->Address.lpSockaddr->sa_family)
      {
        continue;
      }

      ++n;
    }
  }

  /* contiguous block for adapter list */
  struct _llarp_nt_ifaddrs_t* ifa =
      llarp_nt_new0(struct _llarp_nt_ifaddrs_t, n);
  struct _llarp_nt_ifaddrs_t* ift = ifa;

  /* now populate list */
  for(adapter = pAdapterAddresses; adapter; adapter = adapter->Next)
  {
    int unicastIndex = 0;
    for(IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
        unicast; unicast                    = unicast->Next, ++unicastIndex)
    {
      /* ensure IP adapter */
      if(AF_INET != unicast->Address.lpSockaddr->sa_family
         && AF_INET6 != unicast->Address.lpSockaddr->sa_family)
      {
        continue;
      }

      /* address */
      ift->_ifa.ifa_addr = (sockaddr*)&ift->_addr;
      memcpy(ift->_ifa.ifa_addr, unicast->Address.lpSockaddr,
             unicast->Address.iSockaddrLength);

      /* name */
#ifdef DEBUG
      fprintf(stderr, "name:%s IPv4 index:%lu IPv6 index:%lu\n",
              adapter->AdapterName, adapter->IfIndex, adapter->Ipv6IfIndex);
#endif
      ift->_ifa.ifa_name = ift->_name;
      StringCchCopyN(ift->_ifa.ifa_name, 256, adapter->AdapterName, 256);

      /* flags */
      ift->_ifa.ifa_flags = 0;
      if(IfOperStatusUp == adapter->OperStatus)
        ift->_ifa.ifa_flags |= IFF_UP;
      if(IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType)
        ift->_ifa.ifa_flags |= IFF_LOOPBACK;
      if(!(adapter->Flags & IP_ADAPTER_NO_MULTICAST))
        ift->_ifa.ifa_flags |= IFF_MULTICAST;

      /* netmask */
      ift->_ifa.ifa_netmask = (sockaddr*)&ift->_netmask;

      /* pre-Vista must hunt for matching prefix in linked list, otherwise use
       * OnLinkPrefixLength from IP_ADAPTER_UNICAST_ADDRESS structure.
       * FirstPrefix requires Windows XP SP1, from SP1 to pre-Vista provides a
       * single adapter prefix for each IP address.  Vista and later provides
       * host IP address prefix, subnet IP address, and subnet broadcast IP
       * address.  In addition there is a multicast and broadcast address
       * prefix.
       */
      ULONG prefixLength = 0;

#if defined(_WIN32) && (_WIN32_WINNT >= 0x0600)
      /* For a unicast IPv4 address, any value greater than 32 is an illegal
       * value. For a unicast IPv6 address, any value greater than 128 is an
       * illegal value. A value of 255 is commonly used to represent an illegal
       * value.
       *
       * Windows 7 SP1 returns 64 for Teredo links which is incorrect.
       */

#define IN6_IS_ADDR_TEREDO(addr) \
  (((const uint32_t*)(addr))[0] == ntohl(0x20010000))

      if(AF_INET6 == unicast->Address.lpSockaddr->sa_family &&
         /* TunnelType only applies to one interface on the adapter and no
          * convenient method is provided to determine which.
          */
         TUNNEL_TYPE_TEREDO == adapter->TunnelType &&
         /* Test the interface with the known Teredo network prefix.
          */
         IN6_IS_ADDR_TEREDO(
             &((struct sockaddr_in6*)(unicast->Address.lpSockaddr))->sin6_addr)
         &&
         /* Test that this version is actually wrong, subsequent releases from
          * Microsoft may resolve the issue.
          */
         32 != unicast->OnLinkPrefixLength)
      {
#ifdef DEBUG
        fprintf(stderr,
                "IPv6 Teredo tunneling adapter %s prefix length is an "
                "illegal value %lu, overriding to 32.\n",
                adapter->AdapterName, unicast->OnLinkPrefixLength);
#endif
        prefixLength = 32;
      }
      else
        prefixLength = unicast->OnLinkPrefixLength;
#else
      /* The order of linked IP_ADAPTER_UNICAST_ADDRESS structures pointed to by
       * the FirstUnicastAddress member does not have any relationship with the
       * order of linked IP_ADAPTER_PREFIX structures pointed to by the
       * FirstPrefix member.
       *
       * Example enumeration:
       *    [ no subnet ]
       *   ::1/128            - address
       *   ff00::%1/8         - multicast (no IPv6 broadcast)
       *   127.0.0.0/8        - subnet
       *   127.0.0.1/32       - address
       *   127.255.255.255/32 - subnet broadcast
       *   224.0.0.0/4        - multicast
       *   255.255.255.255/32 - broadcast
       *
       * Which differs from most adapters listing three IPv6:
       *   fe80::%10/64       - subnet
       *   fe80::51e9:5fe5:4202:325a%10/128 - address
       *   ff00::%10/8        - multicast
       *
       * !IfOperStatusUp IPv4 addresses are skipped:
       *   fe80::%13/64       - subnet
       *   fe80::d530:946d:e8df:8c91%13/128 - address
       *   ff00::%13/8        - multicast
       *    [ no subnet  ]
       *    [ no address ]
       *   224.0.0.0/4        - multicast
       *   255.255.255.255/32 - broadcast
       *
       * On PTP links no multicast or broadcast addresses are returned:
       *    [ no subnet ]
       *   fe80::5efe:10.203.9.30/128 - address
       *    [ no multicast ]
       *    [ no multicast ]
       *    [ no broadcast ]
       *
       * Active primary IPv6 interfaces are a bit overloaded:
       *   ::/0               - default route
       *   2001::/32          - global subnet
       *   2001:0:4137:9e76:2443:d6:ba87:1a2a/128 - global address
       *   fe80::/64          - link-local subnet
       *   fe80::2443:d6:ba87:1a2a/128 - link-local address
       *   ff00::/8           - multicast
       */

#define IN_LINKLOCAL(a) ((((uint32_t)(a)) & 0xaffff0000) == 0xa9fe0000)

      for(IP_ADAPTER_PREFIX* prefix = adapter->FirstPrefix; prefix;
          prefix                    = prefix->Next)
      {
        LPSOCKADDR lpSockaddr = prefix->Address.lpSockaddr;
        if(lpSockaddr->sa_family != unicast->Address.lpSockaddr->sa_family)
          continue;
        /* special cases */
        /* RFC2863: IPv4 interface not up */
        if(AF_INET == lpSockaddr->sa_family
           && adapter->OperStatus != IfOperStatusUp)
        {
          /* RFC3927: link-local IPv4 always has 16-bit CIDR */
          if(IN_LINKLOCAL(
                 ntohl(((struct sockaddr_in*)(unicast->Address.lpSockaddr))
                           ->sin_addr.s_addr)))
          {
#ifdef DEBUG
            fprintf(stderr,
                    "Assuming 16-bit prefix length for link-local IPv4 "
                    "adapter %s.\n",
                    adapter->AdapterName);
#endif
            prefixLength = 16;
          }
          else
          {
#ifdef DEBUG
            fprintf(stderr, "Prefix length unavailable for IPv4 adapter %s.\n",
                    adapter->AdapterName);
#endif
          }
          break;
        }
        /* default IPv6 route */
        if(AF_INET6 == lpSockaddr->sa_family && 0 == prefix->PrefixLength
           && IN6_IS_ADDR_UNSPECIFIED(
                  &((struct sockaddr_in6*)(lpSockaddr))->sin6_addr))
        {
#ifdef DEBUG
          fprintf(stderr,
                  "Ingoring unspecified address prefix on IPv6 adapter %s.\n",
                  adapter->AdapterName);
#endif
          continue;
        }
        /* Assume unicast address for first prefix of operational adapter */
        if(AF_INET == lpSockaddr->sa_family)
          assert(!IN_MULTICAST(
              ntohl(((struct sockaddr_in*)(lpSockaddr))->sin_addr.s_addr)));
        if(AF_INET6 == lpSockaddr->sa_family)
          assert(!IN6_IS_ADDR_MULTICAST(
              &((struct sockaddr_in6*)(lpSockaddr))->sin6_addr));
        /* Assume subnet or host IP address for XP backward compatibility */

        prefixLength = prefix->PrefixLength;
        break;
      }
#endif /* defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 ) */

      /* map prefix to netmask */
      ift->_ifa.ifa_netmask->sa_family = unicast->Address.lpSockaddr->sa_family;
      switch(unicast->Address.lpSockaddr->sa_family)
      {
        case AF_INET:
          if(0 == prefixLength || prefixLength > 32)
          {
#ifdef DEBUG
            fprintf(stderr,
                    "IPv4 adapter %s prefix length is an illegal value "
                    "%lu, overriding to 32.\n",
                    adapter->AdapterName, prefixLength);
#endif
            prefixLength = 32;
          }
          /* NB: left-shift of full bit-width is undefined in C standard. */
          ((struct sockaddr_in*)ift->_ifa.ifa_netmask)->sin_addr.s_addr =
              htonl(0xffffffffU << (32 - prefixLength));
          break;

        case AF_INET6:
          if(0 == prefixLength || prefixLength > 128)
          {
#ifdef DEBUG
            fprintf(stderr,
                    "IPv6 adapter %s prefix length is an illegal value "
                    "%lu, overriding to 128.\n",
                    adapter->AdapterName, prefixLength);
#endif
            prefixLength = 128;
          }
          for(LONG i = prefixLength, j = 0; i > 0; i -= 8, ++j)
          {
            ((struct sockaddr_in6*)ift->_ifa.ifa_netmask)
                ->sin6_addr.s6_addr[j] =
                i >= 8 ? 0xff : (ULONG)((0xffU << (8 - i)) & 0xffU);
          }
          break;
      }

      /* next */
      if(k++ < (n - 1))
      {
        ift->_ifa.ifa_next = (struct llarp_nt_ifaddrs_t*)(ift + 1);
        ift                = (struct _llarp_nt_ifaddrs_t*)(ift->_ifa.ifa_next);
      }
	  else
      {
            ift->_ifa.ifa_next = nullptr;
	  }
    }
  }

  if(pAdapterAddresses)
    _llarp_nt_heap_free(pAdapterAddresses);
  *ifap = (struct llarp_nt_ifaddrs_t*)ifa;
  return TRUE;
}

static unsigned
_llarp_nt_getadaptersaddresses_nametoindex(const char* ifname)
{
  ULONG ifIndex;
  DWORD dwSize = 4096, dwRet;
  IP_ADAPTER_ADDRESSES *pAdapterAddresses = nullptr, *adapter;
  char szAdapterName[256];

  if(!ifname)
    return 0;

  StringCchCopyN(szAdapterName, sizeof(szAdapterName), ifname, 256);
  dwRet = GetAdapterIndex((LPWSTR)szAdapterName, &ifIndex);

  if(!dwRet)
    return ifIndex;
  else
    return 0;

  /* fallback to finding index via iterating adapter list */

  /* loop to handle interfaces coming online causing a buffer overflow
   * between first call to list buffer length and second call to enumerate.
   */
  for(unsigned i = 3; i; i--)
  {
    pAdapterAddresses = (IP_ADAPTER_ADDRESSES*)_llarp_nt_heap_alloc(dwSize);
    dwRet = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER
            | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST,
        nullptr, pAdapterAddresses, &dwSize);

    if(ERROR_BUFFER_OVERFLOW == dwRet)
    {
      _llarp_nt_heap_free(pAdapterAddresses);
      pAdapterAddresses = nullptr;
    }
    else
    {
      break;
    }
  }

  switch(dwRet)
  {
    case ERROR_SUCCESS:
      break;
    case ERROR_BUFFER_OVERFLOW:
      errno = ENOBUFS;
      if(pAdapterAddresses)
        _llarp_nt_heap_free(pAdapterAddresses);
      return 0;
    default:
      errno = _doserrno;
      if(pAdapterAddresses)
        _llarp_nt_heap_free(pAdapterAddresses);
      return 0;
  }

  for(adapter = pAdapterAddresses; adapter; adapter = adapter->Next)
  {
    if(0 == strcmp(szAdapterName, adapter->AdapterName))
    {
      //ifIndex = AF_INET6 == iffamily ? adapter->Ipv6IfIndex : adapter->IfIndex;
      _llarp_nt_heap_free(pAdapterAddresses);
      return ifIndex;
    }
  }

  if(pAdapterAddresses)
    _llarp_nt_heap_free(pAdapterAddresses);
  return 0;
}

// the emulated getifaddrs(3) itself.
static bool
llarp_nt_getifaddrs(struct llarp_nt_ifaddrs_t** ifap)
{
  assert(nullptr != ifap);
#ifdef DEBUG
  fprintf(stderr, "llarp_nt_getifaddrs (ifap:%p error:%p)\n", (void*)ifap,
          (void*)errno);
#endif
  return _llarp_nt_getadaptersaddresses(ifap);
}

static void
llarp_nt_freeifaddrs(struct llarp_nt_ifaddrs_t* ifa)
{
  if(!ifa)
    return;
  free(ifa);
}

// emulated if_nametoindex(3)
static unsigned
llarp_nt_if_nametoindex(const char* ifname)
{
  if(!ifname)
    return 0;
  return _llarp_nt_getadaptersaddresses_nametoindex(ifname);
}

// fix up names for win32
#define ifaddrs llarp_nt_ifaddrs_t
#define getifaddrs llarp_nt_getifaddrs
#define freeifaddrs llarp_nt_freeifaddrs
#define if_nametoindex llarp_nt_if_nametoindex
#endif

// jeff's original code
bool
llarp_getifaddr(const char* ifname, int af, struct sockaddr* addr)
{
  ifaddrs* ifa = nullptr;
  bool found   = false;
  socklen_t sl = sizeof(sockaddr_in6);
  if(af == AF_INET)
    sl = sizeof(sockaddr_in);

#ifndef _WIN32
  if(getifaddrs(&ifa) == -1)
#else
  if(!getifaddrs(&ifa))
#endif
    return false;
  ifaddrs* i = ifa;
  while(i)
  {
    if(i->ifa_addr)
    {
      // llarp::LogInfo(__FILE__, "scanning ", i->ifa_name, " af: ",
      // std::to_string(i->ifa_addr->sa_family));
      if(llarp::StrEq(i->ifa_name, ifname) && i->ifa_addr->sa_family == af)
      {
        // can't do this here
        // llarp::Addr a(*i->ifa_addr);
        // if(!a.isPrivate())
        //{
        // llarp::LogInfo(__FILE__, "found ", ifname, " af: ", af);
        memcpy(addr, i->ifa_addr, sl);
        if(af == AF_INET6)
        {
          // set scope id
          sockaddr_in6* ip6addr  = (sockaddr_in6*)addr;
          ip6addr->sin6_scope_id = if_nametoindex(ifname);
          ip6addr->sin6_flowinfo = 0;
        }
        found = true;
        break;
      }
      //}
    }
    i = i->ifa_next;
  }
  if(ifa)
    freeifaddrs(ifa);
  return found;
}

struct privatesInUse
llarp_getPrivateIfs()
{
  struct privatesInUse result;
  // mark all available for use
  result.ten      = false;
  result.oneSeven = false;
  result.oneNine  = false;

  ifaddrs* ifa = nullptr;

#ifndef _WIN32
  if(getifaddrs(&ifa) == -1)
#else
  if(!getifaddrs(&ifa))
#endif
    return result;
  ifaddrs* i = ifa;
  while(i)
  {
    if(i->ifa_addr && i->ifa_addr->sa_family == AF_INET)
    {
      // llarp::LogInfo("scanning ", i->ifa_name, " af: ",
      // std::to_string(i->ifa_addr->sa_family));
      llarp::Addr test(*i->ifa_addr);
      uint32_t byte = test.getHostLong();
      if(test.isTenPrivate(byte))
      {
        llarp::LogDebug("private interface ", i->ifa_name, " ", test, " found");
        result.ten = true;
      }
      else if(test.isOneSevenPrivate(byte))
      {
        llarp::LogDebug("private interface ", i->ifa_name, " ", test, " found");
        result.oneSeven = true;
      }
      else if(test.isOneNinePrivate(byte))
      {
        llarp::LogDebug("private interface ", i->ifa_name, " ", test, " found");
        result.oneNine = true;
      }
    }
    i = i->ifa_next;
  }
  if(ifa)
    freeifaddrs(ifa);
  return result;
}

namespace llarp
{
  bool
  GetBestNetIF(std::string& ifname, int af)
  {
    ifaddrs* ifa = nullptr;
    bool found   = false;
#ifndef _WIN32
    if(getifaddrs(&ifa) == -1)
#else
    if(!getifaddrs(&ifa))
#endif
      return false;
    ifaddrs* i = ifa;
    while(i)
    {
      if(i->ifa_addr)
      {
        if(i->ifa_addr->sa_family == af)
        {
          llarp::Addr a(*i->ifa_addr);
          if(!(a.isPrivate() || a.isLoopback() || (a.getHostLong() == 0)))
          {
            ifname = i->ifa_name;
            found  = true;
            break;
          }
        }
      }
      i = i->ifa_next;
    }
    if(ifa)
      freeifaddrs(ifa);
    return found;
  }

  std::string
  findFreePrivateRange()
  {
    // pick ip
    struct privatesInUse ifsInUse = llarp_getPrivateIfs();
    std::string ip                = "";
    if(!ifsInUse.ten)
    {
      ip = "10.200.0.1/24";
    }
    else if(!ifsInUse.oneSeven)
    {
      ip = "172.16.10.1/24";
    }
    else if(!ifsInUse.oneNine)
    {
      ip = "192.168.10.1/24";
    }
    else
    {
      llarp::LogError(
          "Couldn't easily detect a private range to map lokinet onto");
      return "";
    }
    llarp::LogDebug("Detected " + ip
                    + " is available for use, configuring as such");
    return ip;
  }

  std::string
  findFreeLokiTunIfName()
  {
    uint8_t num = 0;
    while(num < 255)
    {
      std::stringstream ifname_ss;
      ifname_ss << "lokitun" << num;
      std::string iftestname = ifname_ss.str();
      struct sockaddr addr;
      bool found = llarp_getifaddr(iftestname.c_str(), AF_INET, &addr);
      if(!found)
      {
        llarp::LogDebug("Detected " + iftestname
                        + " is available for use, configuring as such");
        break;
      }
      num++;
    }
    if(num == 255)
    {
      // llarp::LogError("Could not find any free lokitun interface names");
      return "";
    }
// include lokitun prefix to communicate result is valid
#if defined(ANDROID) || defined(RPI)
    char buff[IFNAMSIZ + 1] = {0};
    snprintf(buff, sizeof(buff), "lokitun%u", num);
    return buff;
#else
    return "lokitun" + std::to_string(num);
#endif
  }

  bool
  GetIFAddr(const std::string& ifname, Addr& addr, int af)
  {
    sockaddr_storage s;
    sockaddr* sptr = (sockaddr*)&s;
    if(!llarp_getifaddr(ifname.c_str(), af, sptr))
      return false;
    addr = *sptr;
    return true;
  }

  bool
  AllInterfaces(int af, Addr& result)
  {
    if(af == AF_INET)
    {
      sockaddr_in addr;
      addr.sin_family      = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port        = htons(0);
      result               = addr;
      return true;
    }
    else if(af == AF_INET6)
    {
      sockaddr_in6 addr6;
      addr6.sin6_family = AF_INET6;
      addr6.sin6_port   = htons(0);
      addr6.sin6_addr   = IN6ADDR_ANY_INIT;
      result            = addr6;
      return true;
    }
    else
    {
      // TODO: implement sockaddr_ll
    }
    return false;
  }

  bool
  IsBogon(const in6_addr& addr)
  {
#ifdef TESTNET
    (void)addr;
    return false;
#else
    if(!ipv6_is_siit(addr))
      return false;
    return IsIPv4Bogon(ipaddr_ipv4_bits(addr.s6_addr[12], addr.s6_addr[13],
                                        addr.s6_addr[14], addr.s6_addr[15]));
#endif
  }

  bool
  IsBogonRange(__attribute__((unused)) const in6_addr& host,
               __attribute__((unused)) const in6_addr& netmask)
  {
    // TODO: implement me
    return true;
  }

  bool
  IsIPv4Bogon(const huint32_t& addr)
  {
    static std::vector< IPRange > bogonRanges = {
        iprange_ipv4(0, 0, 0, 0, 8),       iprange_ipv4(10, 0, 0, 0, 8),
        iprange_ipv4(21, 0, 0, 0, 8),      iprange_ipv4(100, 64, 0, 0, 10),
        iprange_ipv4(127, 0, 0, 0, 8),     iprange_ipv4(169, 254, 0, 0, 8),
        iprange_ipv4(172, 16, 0, 0, 12),   iprange_ipv4(192, 0, 0, 0, 24),
        iprange_ipv4(192, 0, 2, 0, 24),    iprange_ipv4(192, 88, 99, 0, 24),
        iprange_ipv4(192, 168, 0, 0, 16),  iprange_ipv4(198, 18, 0, 0, 15),
        iprange_ipv4(198, 51, 100, 0, 24), iprange_ipv4(203, 0, 113, 0, 24),
        iprange_ipv4(224, 0, 0, 0, 4),     iprange_ipv4(240, 0, 0, 0, 4)};

    for(const auto& bogon : bogonRanges)
    {
      if(bogon.Contains(addr))
      {
        return true;
      }
    }
    return false;
  }
}  // namespace llarp
