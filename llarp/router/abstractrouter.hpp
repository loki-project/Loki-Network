#ifndef LLARP_ABSTRACT_ROUTER_HPP
#define LLARP_ABSTRACT_ROUTER_HPP

#include <config/config.hpp>
#include <config/key_manager.hpp>
#include <memory>
#include <util/types.hpp>
#include <util/status.hpp>
#include <router/i_outbound_message_handler.hpp>
#include <vector>
#include <ev/ev.h>
#include <functional>
#include <router_contact.hpp>
#include <tooling/router_event.hpp>
#include <peerstats/peer_db.hpp>

#ifdef LOKINET_HIVE
#include "tooling/router_event.hpp"
#endif

struct llarp_buffer_t;
struct llarp_dht_context;
struct llarp_nodedb;
struct llarp_threadpool;

namespace lokimq
{
  class LokiMQ;
}

namespace llarp
{
  class Logic;
  struct Config;
  struct RouterID;
  struct ILinkMessage;
  struct ILinkSession;
  struct PathID_t;
  struct Profiling;
  struct SecretKey;
  struct Signature;
  struct IOutboundMessageHandler;
  struct IOutboundSessionMaker;
  struct ILinkManager;
  struct I_RCLookupHandler;
  struct RoutePoker;

  namespace exit
  {
    struct Context;
  }

  namespace rpc
  {
    struct LokidRpcClient;
  }

  namespace path
  {
    struct PathContext;
  }

  namespace routing
  {
    struct IMessageHandler;
  }

  namespace service
  {
    struct Context;
  }

  namespace thread
  {
    class ThreadPool;
  }

  namespace vpn
  {
    class Platform;
  }

  using LMQ_ptr = std::shared_ptr<lokimq::LokiMQ>;

  struct AbstractRouter
  {
#ifdef LOKINET_HIVE
    tooling::RouterHive* hive = nullptr;
#endif

    virtual ~AbstractRouter() = default;

    virtual bool
    HandleRecvLinkMessageBuffer(ILinkSession* from, const llarp_buffer_t& msg) = 0;

    virtual LMQ_ptr
    lmq() const = 0;

    virtual vpn::Platform*
    GetVPNPlatform() const = 0;

    virtual std::shared_ptr<rpc::LokidRpcClient>
    RpcClient() const = 0;

    virtual std::shared_ptr<Logic>
    logic() const = 0;

    virtual llarp_dht_context*
    dht() const = 0;

    virtual llarp_nodedb*
    nodedb() const = 0;

    virtual const path::PathContext&
    pathContext() const = 0;

    virtual path::PathContext&
    pathContext() = 0;

    virtual const RouterContact&
    rc() const = 0;

    virtual exit::Context&
    exitContext() = 0;

    virtual std::shared_ptr<KeyManager>
    keyManager() const = 0;

    virtual const SecretKey&
    identity() const = 0;

    virtual const SecretKey&
    encryption() const = 0;

    virtual Profiling&
    routerProfiling() = 0;

    virtual llarp_ev_loop_ptr
    netloop() const = 0;

    /// call function in crypto worker
    virtual void QueueWork(std::function<void(void)>) = 0;

    /// call function in disk io thread
    virtual void QueueDiskIO(std::function<void(void)>) = 0;

    virtual std::shared_ptr<Config>
    GetConfig() const
    {
      return nullptr;
    }

    virtual service::Context&
    hiddenServiceContext() = 0;

    virtual const service::Context&
    hiddenServiceContext() const = 0;

    virtual IOutboundMessageHandler&
    outboundMessageHandler() = 0;

    virtual IOutboundSessionMaker&
    outboundSessionMaker() = 0;

    virtual ILinkManager&
    linkManager() = 0;

    virtual RoutePoker&
    routePoker() = 0;

    virtual I_RCLookupHandler&
    rcLookupHandler() = 0;

    virtual std::shared_ptr<PeerDb>
    peerDb() = 0;

    virtual bool
    Sign(Signature& sig, const llarp_buffer_t& buf) const = 0;

    virtual bool
    Configure(std::shared_ptr<Config> conf, bool isRouter, llarp_nodedb* nodedb) = 0;

    virtual bool
    IsServiceNode() const = 0;

    virtual bool
    StartRpcServer() = 0;

    virtual bool
    Run() = 0;

    virtual bool
    IsRunning() const = 0;

    virtual bool
    LooksAlive() const = 0;

    /// stop running the router logic gracefully
    virtual void
    Stop() = 0;

    /// non gracefully stop the router
    virtual void
    Die() = 0;

    /// pump low level links
    virtual void
    PumpLL() = 0;

    virtual bool
    IsBootstrapNode(RouterID r) const = 0;

    virtual const byte_t*
    pubkey() const = 0;

    /// connect to N random routers
    virtual void
    ConnectToRandomRouters(int N) = 0;

    virtual bool
    TryConnectAsync(RouterContact rc, uint16_t tries) = 0;

    /// called by link when a remote session has no more sessions open
    virtual void
    SessionClosed(RouterID remote) = 0;

    /// returns system clock milliseconds since epoch
    virtual llarp_time_t
    Now() const = 0;

    /// returns milliseconds since started
    virtual llarp_time_t
    Uptime() const = 0;

    virtual bool
    GetRandomGoodRouter(RouterID& r) = 0;

    virtual bool
    SendToOrQueue(
        const RouterID& remote, const ILinkMessage* msg, SendStatusHandler handler = nullptr) = 0;

    virtual void
    PersistSessionUntil(const RouterID& remote, llarp_time_t until) = 0;

    virtual bool
    ParseRoutingMessageBuffer(
        const llarp_buffer_t& buf, routing::IMessageHandler* h, const PathID_t& rxid) = 0;

    /// count the number of service nodes we are connected to
    virtual size_t
    NumberOfConnectedRouters() const = 0;

    /// count the number of clients that are connected to us
    virtual size_t
    NumberOfConnectedClients() const = 0;

    virtual bool
    GetRandomConnectedRouter(RouterContact& result) const = 0;

    virtual void
    HandleDHTLookupForExplore(RouterID remote, const std::vector<RouterContact>& results) = 0;

    virtual void SetDownHook(std::function<void(void)>){};

    /// lookup router by pubkey
    /// if we are a service node this is done direct otherwise it's done via
    /// path
    virtual void
    LookupRouter(RouterID remote, RouterLookupHandler resultHandler) = 0;

    /// check if newRc matches oldRC and update local rc for this remote contact
    /// if valid
    /// returns true on valid and updated
    /// returns false otherwise
    virtual bool
    CheckRenegotiateValid(RouterContact newRc, RouterContact oldRC) = 0;

    /// set router's service node whitelist
    virtual void
    SetRouterWhitelist(const std::vector<RouterID> routers) = 0;

    /// visit each connected link session
    virtual void
    ForEachPeer(std::function<void(const ILinkSession*, bool)> visit, bool randomize) const = 0;

    virtual bool
    ConnectionToRouterAllowed(const RouterID& router) const = 0;

    /// return true if we have at least 1 session to this router in either
    /// direction
    virtual bool
    HasSessionTo(const RouterID& router) const = 0;

    virtual uint32_t
    NextPathBuildNumber() = 0;

    virtual std::string
    ShortName() const = 0;

    virtual util::StatusObject
    ExtractStatus() const = 0;

    /// gossip an rc if required
    virtual void
    GossipRCIfNeeded(const RouterContact rc) = 0;

    /// Templated convenience function to generate a RouterHive event and
    /// delegate to non-templated (and overridable) function for handling.
    template <class EventType, class... Params>
    void
    NotifyRouterEvent([[maybe_unused]] Params&&... args) const
    {
      // TODO: no-op when appropriate
      auto event = std::make_unique<EventType>(args...);
      HandleRouterEvent(std::move(event));
    }

   protected:
    /// Virtual function to handle RouterEvent. HiveRouter overrides this in
    /// order to inject the event. The default implementation in Router simply
    /// logs it.
    virtual void
    HandleRouterEvent(tooling::RouterEventPtr event) const = 0;
  };
}  // namespace llarp

#endif
