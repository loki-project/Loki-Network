#include <router/route_poker.hpp>
#include <router/abstractrouter.hpp>
#include <net/route.hpp>
#include <service/context.hpp>
#include <unordered_set>

namespace llarp
{
  void
  RoutePoker::AddRoute(huint32_t ip)
  {
    m_PokedRoutes.emplace(ip, m_CurrentGateway);
    if (m_CurrentGateway.h == 0)
    {
      llarp::LogDebug("RoutePoker::AddRoute no current gateway, cannot enable route.");
    }
    else if (m_Enabled or m_Enabling)
    {
      llarp::LogInfo(
          "RoutePoker::AddRoute enabled, enabling route to ", ip, " via ", m_CurrentGateway);
      EnableRoute(ip, m_CurrentGateway);
    }
    else
    {
      llarp::LogDebug("RoutePoker::AddRoute disabled, not enabling route.");
    }
  }

  void
  RoutePoker::DisableRoute(huint32_t ip, huint32_t gateway)
  {
    net::DelRoute(ip.ToString(), gateway.ToString());
  }

  void
  RoutePoker::EnableRoute(huint32_t ip, huint32_t gateway)
  {
    net::AddRoute(ip.ToString(), gateway.ToString());
  }

  void
  RoutePoker::DelRoute(huint32_t ip)
  {
    const auto itr = m_PokedRoutes.find(ip);
    if (itr == m_PokedRoutes.end())
      return;
    m_PokedRoutes.erase(itr);

    if (m_Enabled)
      DisableRoute(itr->first, itr->second);
  }

  void
  RoutePoker::Init(AbstractRouter* router, bool enable)
  {
    m_Router = router;
    m_Enabled = enable;
    m_CurrentGateway = {0};
  }

  void
  RoutePoker::DeleteAllRoutes()
  {
    // DelRoute will check enabled, so no need here
    for (const auto& [ip, gateway] : m_PokedRoutes)
      DelRoute(ip);
  }

  void
  RoutePoker::DisableAllRoutes()
  {
    for (const auto& [ip, gateway] : m_PokedRoutes)
      DisableRoute(ip, gateway);
  }

  void
  RoutePoker::EnableAllRoutes()
  {
    for (auto& [ip, gateway] : m_PokedRoutes)
    {
      gateway = m_CurrentGateway;
      EnableRoute(ip, m_CurrentGateway);
    }
  }

  RoutePoker::~RoutePoker()
  {
    for (const auto& [ip, gateway] : m_PokedRoutes)
    {
      if (gateway.h)
        net::DelRoute(ip.ToString(), gateway.ToString());
    }
  }

  std::optional<huint32_t>
  RoutePoker::GetDefaultGateway() const
  {
    if (not m_Router)
      throw std::runtime_error("Attempting to use RoutePoker before calling Init");

    const auto ep = m_Router->hiddenServiceContext().GetDefault();
    const auto gateways = net::GetGatewaysNotOnInterface(ep->GetIfName());
    if (gateways.empty())
    {
      return std::nullopt;
    }
    huint32_t addr{};
    addr.FromString(gateways[0]);
    return addr;
  }

  void
  RoutePoker::Update()
  {
    if (not m_Router)
      throw std::runtime_error("Attempting to use RoutePoker before calling Init");

    // check for network
    const auto maybe = GetDefaultGateway();
    if (not maybe.has_value())
    {
      LogError("Network is down");
      // mark network lost
      m_HasNetwork = false;
      return;
    }
    const huint32_t gateway = *maybe;

    const bool gatewayChanged = m_CurrentGateway.h != 0 and m_CurrentGateway != gateway;

    if (m_CurrentGateway != gateway)
    {
      LogInfo("found default gateway: ", gateway);
      m_CurrentGateway = gateway;
      if (m_Enabling)
      {
        EnableAllRoutes();
        Up();
      }
    }
    // revive network connectitivity on gateway change or network wakeup
    if (gatewayChanged or not m_HasNetwork)
    {
      LogInfo("our network changed, thawing router state");
      m_Router->Thaw();
      m_HasNetwork = true;
    }
  }

  void
  RoutePoker::Enable()
  {
    if (m_Enabled)
      return;

    m_Enabling = true;
    Update();
    m_Enabling = false;
    m_Enabled = true;
  }

  void
  RoutePoker::Disable()
  {
    if (not m_Enabled)
      return;

    DisableAllRoutes();
    m_Enabled = false;
  }

  void
  RoutePoker::Up()
  {
    // explicit route pokes for first hops
    m_Router->ForEachPeer(
        [&](auto session, auto) mutable { AddRoute(session->GetRemoteEndpoint().toIP()); }, false);
    // add default route
    const auto ep = m_Router->hiddenServiceContext().GetDefault();
    net::AddDefaultRouteViaInterface(ep->GetIfName());
  }

  void
  RoutePoker::Down()
  {
    // unpoke routes for first hops
    m_Router->ForEachPeer(
        [&](auto session, auto) mutable { DelRoute(session->GetRemoteEndpoint().toIP()); }, false);
    // remove default route
    const auto ep = m_Router->hiddenServiceContext().GetDefault();
    net::DelDefaultRouteViaInterface(ep->GetIfName());
  }

}  // namespace llarp
