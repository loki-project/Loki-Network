#include <dns/iptracker.hpp>

dns_iptracker g_dns_iptracker;

void
dns_iptracker_init()
{
  /*
  g_dns_iptracker.interfaces = llarp_getPrivateIfs();
  llarp::LogInfo("Interface uses 10.x.x.x? ",
                 g_dns_iptracker.interfaces.ten ? "Yes" : "No");
  g_dns_iptracker.used_privates = g_dns_iptracker.interfaces;
  llarp::LogInfo("We used 10.x.x.x? ",
                 g_dns_iptracker.used_privates.ten ? "Yes" : "No");
  */

  // disable all possibilities unless you setup a tunGateway

  g_dns_iptracker.used_privates.ten      = true;
  g_dns_iptracker.used_privates.oneSeven = true;
  g_dns_iptracker.used_privates.oneNine  = true;
}

// not sure we want tunGatewayIP... we'll know when we get further
bool
dns_iptracker_setup_dotLokiLookup(dotLokiLookup *dll, ABSL_ATTRIBUTE_UNUSED
                                  llarp::huint32_t tunGatewayIp)
{
  dll->ip_tracker = &g_dns_iptracker;
  return true;
}

// FIXME: pass in b32addr of client
bool
dns_iptracker_setup(dns_iptracker *iptracker, llarp::huint32_t tunGatewayIp)
{
  if(!iptracker)
    iptracker = &g_dns_iptracker;  // FIXME: god forgive I'm tired
  // struct in_addr *addr = tunGatewayIp.addr4();
  // unsigned char *ip    = (unsigned char *)&(addr->s_addr);
  unsigned char *ip = (unsigned char *)&(tunGatewayIp.h);
  /*
    llarp::LogInfo("iptracker setup: (", std::to_string(ip[0]), ").[",
                   std::to_string(ip[1]), '.', std::to_string(ip[2]), "].",
                   std::to_string(ip[3]));
  */
  std::unique_ptr< ip_range > range(new ip_range);
  range->octet2 = ip[1];  // 2nd octet
  range->octet3 = ip[2];  // 3rd octet
  // FIXME: look up any static mappings to discount
  range->left = 252;
  // 4th octet, probably 1, set it
  struct dns_pointer *result = new dns_pointer;
  result->hostResult         = tunGatewayIp;
  // tunGatewayIp.CopyInto(result->hostResult);
  // result->b32addr = ; // FIXME: should be our HS addr
  range->used[ip[3]] = result;  // claim tun IP

  // save tun range in tracker
  // FIXME: forcing one and only one range
  if(ip[0] == 10)
  {
    iptracker->used_ten_ips.push_back(std::move(range));
    iptracker->used_privates.ten = false;
  }
  else if(ip[0] == 172)
  {
    iptracker->used_seven_ips.push_back(std::move(range));
    iptracker->used_privates.oneSeven = false;
  }
  else if(ip[0] == 192)
  {
    iptracker->used_nine_ips.push_back(std::move(range));
    iptracker->used_privates.oneNine = false;
  }
  else
  {
    return false;
  }
  return true;
}

inline struct dns_pointer *
dns_iptracker_allocate_range(std::unique_ptr< ip_range > &range, uint8_t first)
{
  // we have an IP
  llarp::LogDebug("Range has ", (unsigned int)range->left, " ips left");
  range->left--;  // use it up
  struct dns_pointer *result = new dns_pointer;
  llarp::Addr ip(first, range->octet2, range->octet3,
                 range->left + 2);  // why plus 2? to start at .2
  llarp::LogDebug("Allocated ", ip);
  // result->hostResult = new sockaddr;
  // ip.CopyInto(result->hostResult);
  result->hostResult = ip.xtohl();

  // make an address and place into this sockaddr
  range->used[range->left + 2] = result;
  return result;
}

struct dns_pointer *
dns_iptracker_check_range(std::vector< std::unique_ptr< ip_range > > &ranges,
                          uint8_t first)
{
  // tens not all used up
  if(ranges.size())
  {
    // FIXME: maybe find_if where left not 0
    // find a range
    for(auto it = ranges.begin(); it != ranges.end(); ++it)
    {
      if((*it)->left)
      {
        struct dns_pointer *result = dns_iptracker_allocate_range(*it, first);
        if(!(*it)->left)
        {
          // all used up
          // FIXME: are there any more octets available?
        }
        return result;
      }
    }
  }
  else
  {
    // create one
    std::unique_ptr< ip_range > new_range(new ip_range);
    new_range->octet2 = 0;
    switch(first)
    {
      case 172:
      {
        // FIXME: goes up to 31...
        new_range->octet2 = 16;
        break;
      }
      case 192:
      {
        new_range->octet2 = 168;
        break;
      }
    }
    new_range->octet3 = 0;  // FIXME: counter (0-255)
    // CHECK: planning a /24 but maybe that's too wide for broadcasts
    new_range->left = 252;  // 0 is net, 1 is gw, 255 is broadcast
    ranges.push_back(std::move(new_range));
    // don't need to check if we're out since this is fresh range
    return dns_iptracker_allocate_range(ranges[0], first);
  }
  return nullptr;
}

struct dns_pointer *
dns_iptracker_get_free()
{
  return dns_iptracker_get_free(&g_dns_iptracker);
}

struct dns_pointer *
dns_iptracker_get_free(dns_iptracker *iptracker)
{
  llarp::LogInfo("Was 10.x.x.x already in-use on start? ",
                 iptracker->used_privates.ten ? "Yes" : "No");
  if(!iptracker->used_privates.ten)
  {
    struct dns_pointer *test =
        dns_iptracker_check_range(iptracker->used_ten_ips, 10);
    if(test)
    {
      return test;
    }
  }
  llarp::LogInfo("Was 172.16.x.x  already in-use on start? ",
                 iptracker->used_privates.oneSeven ? "Yes" : "No");
  if(!iptracker->used_privates.oneSeven)
  {
    struct dns_pointer *test =
        dns_iptracker_check_range(iptracker->used_seven_ips, 172);
    if(test)
    {
      return test;
    }
  }
  llarp::LogInfo("Was 192.168.x.x already in-use on start? ",
                 iptracker->used_privates.oneNine ? "Yes" : "No");
  if(!iptracker->used_privates.oneNine)
  {
    struct dns_pointer *test =
        dns_iptracker_check_range(iptracker->used_nine_ips, 192);
    if(test)
    {
      return test;
    }
  }
  return nullptr;
}
