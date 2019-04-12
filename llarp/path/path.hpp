#ifndef LLARP_PATH_HPP
#define LLARP_PATH_HPP

#include <crypto/encrypted_frame.hpp>
#include <crypto/types.hpp>
#include <messages/relay.hpp>
#include <path/path_types.hpp>
#include <path/pathbuilder.hpp>
#include <path/pathset.hpp>
#include <router_id.hpp>
#include <routing/handler.hpp>
#include <routing/message.hpp>
#include <service/Intro.hpp>
#include <util/aligned.hpp>
#include <util/threading.hpp>
#include <util/time.hpp>

#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace llarp
{
  class Logic;
  struct AbstractRouter;
  struct Crypto;
  struct LR_CommitMessage;
  struct LR_CommitRecord;

  namespace path
  {
    /// maximum path length
    constexpr size_t max_len = 8;
    /// default path length
    constexpr size_t default_len = 4;
    /// pad messages to the nearest this many bytes
    constexpr size_t pad_size = 128;
    /// default path lifetime in ms
    constexpr llarp_time_t default_lifetime = 10 * 60 * 1000;
    /// after this many ms a path build times out
    constexpr llarp_time_t build_timeout = 15000;

    /// measure latency every this interval ms
    constexpr llarp_time_t latency_interval = 5000;

    /// if a path is inactive for this amount of time it's dead
    constexpr llarp_time_t alive_timeout = 60000;

    struct TransitHopInfo
    {
      TransitHopInfo() = default;
      TransitHopInfo(const TransitHopInfo& other);
      TransitHopInfo(const RouterID& down, const LR_CommitRecord& record);

      PathID_t txID, rxID;
      RouterID upstream;
      RouterID downstream;

      std::ostream&
      print(std::ostream& stream, int level, int spaces) const;

      bool
      operator==(const TransitHopInfo& other) const
      {
        return txID == other.txID && rxID == other.rxID
            && upstream == other.upstream && downstream == other.downstream;
      }

      bool
      operator!=(const TransitHopInfo& other) const
      {
        return !(*this == other);
      }

      bool
      operator<(const TransitHopInfo& other) const
      {
        return txID < other.txID || rxID < other.rxID
            || upstream < other.upstream || downstream < other.downstream;
      }

      struct PathIDHash
      {
        std::size_t
        operator()(const PathID_t& a) const
        {
          return AlignedBuffer< PathID_t::SIZE >::Hash()(a);
        }
      };

      struct Hash
      {
        std::size_t
        operator()(TransitHopInfo const& a) const
        {
          std::size_t idx0 = RouterID::Hash()(a.upstream);
          std::size_t idx1 = RouterID::Hash()(a.downstream);
          std::size_t idx2 = PathIDHash()(a.txID);
          std::size_t idx3 = PathIDHash()(a.rxID);
          return idx0 ^ idx1 ^ idx2 ^ idx3;
        }
      };
    };

    inline std::ostream&
    operator<<(std::ostream& out, const TransitHopInfo& info)
    {
      return info.print(out, -1, -1);
    }

    struct IHopHandler
    {
      virtual ~IHopHandler(){};

      virtual bool
      Expired(llarp_time_t now) const = 0;

      virtual bool
      ExpiresSoon(llarp_time_t now, llarp_time_t dlt) const = 0;

      /// send routing message and increment sequence number
      virtual bool
      SendRoutingMessage(const llarp::routing::IMessage* msg,
                         llarp::AbstractRouter* r) = 0;

      // handle data in upstream direction
      virtual bool
      HandleUpstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                     AbstractRouter* r) = 0;

      // handle data in downstream direction
      virtual bool
      HandleDownstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                       AbstractRouter* r) = 0;

      /// return timestamp last remote activity happened at
      virtual llarp_time_t
      LastRemoteActivityAt() const = 0;

      uint64_t
      NextSeqNo()
      {
        return m_SequenceNum++;
      }

     protected:
      uint64_t m_SequenceNum = 0;
    };

    struct TransitHop : public IHopHandler,
                        public llarp::routing::IMessageHandler
    {
      TransitHop();

      TransitHop(const TransitHop& other);

      TransitHopInfo info;
      SharedSecret pathKey;
      ShortHash nonceXOR;
      llarp_time_t started = 0;
      // 10 minutes default
      llarp_time_t lifetime = default_lifetime;
      llarp_proto_version_t version;
      llarp_time_t m_LastActivity = 0;

      bool
      IsEndpoint(const RouterID& us) const
      {
        return info.upstream == us;
      }

      llarp_time_t
      ExpireTime() const;

      llarp_time_t
      LastRemoteActivityAt() const override
      {
        return m_LastActivity;
      }

      std::ostream&
      print(std::ostream& stream, int level, int spaces) const;

      bool
      Expired(llarp_time_t now) const override;

      bool
      ExpiresSoon(llarp_time_t now, llarp_time_t dlt) const override
      {
        return now >= ExpireTime() - dlt;
      }

      // send routing message when end of path
      bool
      SendRoutingMessage(const llarp::routing::IMessage* msg,
                         AbstractRouter* r) override;

      // handle routing message when end of path
      bool
      HandleRoutingMessage(const llarp::routing::IMessage* msg,
                           AbstractRouter* r);

      bool
      HandleDataDiscardMessage(const llarp::routing::DataDiscardMessage* msg,
                               AbstractRouter* r) override;

      bool
      HandlePathConfirmMessage(const llarp::routing::PathConfirmMessage* msg,
                               AbstractRouter* r) override;
      bool
      HandlePathTransferMessage(const llarp::routing::PathTransferMessage* msg,
                                AbstractRouter* r) override;
      bool
      HandlePathLatencyMessage(const llarp::routing::PathLatencyMessage* msg,
                               AbstractRouter* r) override;

      bool
      HandleObtainExitMessage(const llarp::routing::ObtainExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleUpdateExitVerifyMessage(
          const llarp::routing::UpdateExitVerifyMessage* msg,
          AbstractRouter* r) override;

      bool
      HandleTransferTrafficMessage(
          const llarp::routing::TransferTrafficMessage* msg,
          AbstractRouter* r) override;

      bool
      HandleUpdateExitMessage(const llarp::routing::UpdateExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleGrantExitMessage(const llarp::routing::GrantExitMessage* msg,
                             AbstractRouter* r) override;
      bool
      HandleRejectExitMessage(const llarp::routing::RejectExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleCloseExitMessage(const llarp::routing::CloseExitMessage* msg,
                             AbstractRouter* r) override;

      bool
      HandleHiddenServiceFrame(__attribute__((
          unused)) const llarp::service::ProtocolFrame* frame) override
      {
        /// TODO: implement me
        llarp::LogWarn("Got hidden service data on transit hop");
        return false;
      }

      bool
      HandleGotIntroMessage(const llarp::dht::GotIntroMessage* msg);

      bool
      HandleDHTMessage(const llarp::dht::IMessage* msg,
                       AbstractRouter* r) override;

      // handle data in upstream direction
      bool
      HandleUpstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                     AbstractRouter* r) override;

      // handle data in downstream direction
      bool
      HandleDownstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                       AbstractRouter* r) override;
    };

    inline std::ostream&
    operator<<(std::ostream& out, const TransitHop& h)
    {
      return h.print(out, -1, -1);
    }

    /// configuration for a single hop when building a path
    struct PathHopConfig : public util::IStateful
    {
      /// path id
      PathID_t txID, rxID;
      // router contact of router
      RouterContact rc;
      // temp public encryption key
      SecretKey commkey;
      /// shared secret at this hop
      SharedSecret shared;
      /// hash of shared secret used for nonce mutation
      ShortHash nonceXOR;
      /// next hop's router id
      RouterID upstream;
      /// nonce for key exchange
      TunnelNonce nonce;
      // lifetime
      llarp_time_t lifetime = default_lifetime;

      ~PathHopConfig();
      PathHopConfig();

      util::StatusObject
      ExtractStatus() const override;
    };

    /// A path we made
    struct Path : public IHopHandler,
                  public llarp::routing::IMessageHandler,
                  public util::IStateful
    {
      using BuildResultHookFunc = std::function< void(Path*) >;
      using CheckForDeadFunc    = std::function< bool(Path*, llarp_time_t) >;
      using DropHandlerFunc =
          std::function< bool(Path*, const PathID_t&, uint64_t) >;
      using HopList = std::vector< PathHopConfig >;
      using DataHandlerFunc =
          std::function< bool(Path*, const service::ProtocolFrame*) >;
      using ExitUpdatedFunc = std::function< bool(Path*) >;
      using ExitClosedFunc  = std::function< bool(Path*) >;
      using ExitTrafficHandlerFunc =
          std::function< bool(Path*, const llarp_buffer_t&, uint64_t) >;
      /// (path, backoff) backoff is 0 on success
      using ObtainedExitHandler = std::function< bool(Path*, llarp_time_t) >;

      HopList hops;

      PathSet* m_PathSet;

      llarp::service::Introduction intro;

      llarp_time_t buildStarted;

      Path(const std::vector< RouterContact >& routers, PathSet* parent,
           PathRole startingRoles);

      util::StatusObject
      ExtractStatus() const override;

      PathRole
      Role() const
      {
        return _role;
      }

      void
      MarkActive(llarp_time_t now)
      {
        m_LastRecvMessage = std::max(now, m_LastRecvMessage);
      }

      /// return true if ALL of the specified roles are supported
      bool
      SupportsAllRoles(PathRole roles) const
      {
        return (_role & roles) == roles;
      }

      /// return true if ANY of the specified roles are supported
      bool
      SupportsAnyRoles(PathRole roles) const
      {
        return roles == ePathRoleAny || (_role & roles) != 0;
      }

      PathStatus
      Status() const
      {
        return _status;
      }

      std::string
      HopsString() const;

      llarp_time_t
      LastRemoteActivityAt() const override
      {
        return m_LastRecvMessage;
      }

      void
      SetBuildResultHook(BuildResultHookFunc func);

      void
      SetExitTrafficHandler(ExitTrafficHandlerFunc handler)
      {
        m_ExitTrafficHandler = handler;
      }

      void
      SetCloseExitFunc(ExitClosedFunc handler)
      {
        m_ExitClosed = handler;
      }

      void
      SetUpdateExitFunc(ExitUpdatedFunc handler)
      {
        m_ExitUpdated = handler;
      }

      void
      SetDataHandler(DataHandlerFunc func)
      {
        m_DataHandler = func;
      }

      void
      SetDropHandler(DropHandlerFunc func)
      {
        m_DropHandler = func;
      }

      void
      SetDeadChecker(CheckForDeadFunc func)
      {
        m_CheckForDead = func;
      }

      void
      EnterState(PathStatus st, llarp_time_t now);

      llarp_time_t
      ExpireTime() const
      {
        return buildStarted + hops[0].lifetime;
      }

      bool
      ExpiresSoon(llarp_time_t now, llarp_time_t dlt = 5000) const override
      {
        return now >= (ExpireTime() - dlt);
      }

      bool
      Expired(llarp_time_t now) const override;

      void
      Tick(llarp_time_t now, AbstractRouter* r);

      bool
      SendRoutingMessage(const llarp::routing::IMessage* msg,
                         llarp::AbstractRouter* r) override;

      bool
      HandleObtainExitMessage(const llarp::routing::ObtainExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleUpdateExitVerifyMessage(
          const llarp::routing::UpdateExitVerifyMessage* msg,
          AbstractRouter* r) override;

      bool
      HandleTransferTrafficMessage(
          const llarp::routing::TransferTrafficMessage* msg,
          AbstractRouter* r) override;

      bool
      HandleUpdateExitMessage(const llarp::routing::UpdateExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleCloseExitMessage(const llarp::routing::CloseExitMessage* msg,
                             AbstractRouter* r) override;
      bool
      HandleGrantExitMessage(const llarp::routing::GrantExitMessage* msg,
                             AbstractRouter* r) override;
      bool
      HandleRejectExitMessage(const llarp::routing::RejectExitMessage* msg,
                              AbstractRouter* r) override;

      bool
      HandleDataDiscardMessage(const llarp::routing::DataDiscardMessage* msg,
                               AbstractRouter* r) override;

      bool
      HandlePathConfirmMessage(const llarp::routing::PathConfirmMessage* msg,
                               AbstractRouter* r) override;

      bool
      HandlePathLatencyMessage(const llarp::routing::PathLatencyMessage* msg,
                               AbstractRouter* r) override;

      bool
      HandlePathTransferMessage(const llarp::routing::PathTransferMessage* msg,
                                AbstractRouter* r) override;

      bool
      HandleHiddenServiceFrame(
          const llarp::service::ProtocolFrame* frame) override;

      bool
      HandleGotIntroMessage(const llarp::dht::GotIntroMessage* msg);

      bool
      HandleDHTMessage(const llarp::dht::IMessage* msg,
                       AbstractRouter* r) override;

      bool
      HandleRoutingMessage(const llarp_buffer_t& buf, AbstractRouter* r);

      // handle data in upstream direction
      bool
      HandleUpstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                     AbstractRouter* r) override;

      // handle data in downstream direction
      bool
      HandleDownstream(const llarp_buffer_t& X, const TunnelNonce& Y,
                       AbstractRouter* r) override;

      bool
      IsReady() const;

      // Is this deprecated?
      // nope not deprecated :^DDDD
      PathID_t
      TXID() const;

      RouterID
      Endpoint() const;

      PubKey
      EndpointPubKey() const;

      bool
      IsEndpoint(const RouterID& router, const PathID_t& path) const;

      PathID_t
      RXID() const;

      RouterID
      Upstream() const;

      std::string
      Name() const;

      void
      AddObtainExitHandler(ObtainedExitHandler handler)
      {
        m_ObtainedExitHooks.push_back(handler);
      }

      bool
      SendExitRequest(const llarp::routing::ObtainExitMessage* msg,
                      AbstractRouter* r);

      bool
      SendExitClose(const llarp::routing::CloseExitMessage* msg,
                    AbstractRouter* r);

     private:
      /// call obtained exit hooks
      bool
      InformExitResult(llarp_time_t b);

      BuildResultHookFunc m_BuiltHook;
      DataHandlerFunc m_DataHandler;
      DropHandlerFunc m_DropHandler;
      CheckForDeadFunc m_CheckForDead;
      ExitUpdatedFunc m_ExitUpdated;
      ExitClosedFunc m_ExitClosed;
      ExitTrafficHandlerFunc m_ExitTrafficHandler;
      std::vector< ObtainedExitHandler > m_ObtainedExitHooks;
      llarp_time_t m_LastRecvMessage     = 0;
      llarp_time_t m_LastLatencyTestTime = 0;
      uint64_t m_LastLatencyTestID       = 0;
      uint64_t m_UpdateExitTX            = 0;
      uint64_t m_CloseExitTX             = 0;
      uint64_t m_ExitObtainTX            = 0;
      PathStatus _status;
      PathRole _role;
    };

    enum PathBuildStatus
    {
      ePathBuildSuccess,
      ePathBuildTimeout,
      ePathBuildReject
    };

    struct PathContext
    {
      PathContext(AbstractRouter* router);
      ~PathContext();

      /// called from router tick function
      void
      ExpirePaths(llarp_time_t now);

      /// called from router tick function
      /// builds all paths we need to build at current tick
      void
      BuildPaths(llarp_time_t now);

      /// called from router tick function
      void
      TickPaths(llarp_time_t now);

      ///  track a path builder with this context
      void
      AddPathBuilder(Builder* set);

      void
      AllowTransit();

      void
      RejectTransit();

      bool
      AllowingTransit() const;

      bool
      HasTransitHop(const TransitHopInfo& info);

      bool
      HandleRelayCommit(const LR_CommitMessage* msg);

      void
      PutTransitHop(std::shared_ptr< TransitHop > hop);

      IHopHandler*
      GetByUpstream(const RouterID& id, const PathID_t& path);

      bool
      TransitHopPreviousIsRouter(const PathID_t& path, const RouterID& r);

      IHopHandler*
      GetPathForTransfer(const PathID_t& topath);

      IHopHandler*
      GetByDownstream(const RouterID& id, const PathID_t& path);

      PathSet*
      GetLocalPathSet(const PathID_t& id);

      routing::IMessageHandler*
      GetHandler(const PathID_t& id);

      bool
      ForwardLRCM(const RouterID& nextHop,
                  const std::array< EncryptedFrame, 8 >& frames);

      bool
      HopIsUs(const RouterID& k) const;

      bool
      HandleLRUM(const RelayUpstreamMessage* msg);

      bool
      HandleLRDM(const RelayDownstreamMessage* msg);

      void
      AddOwnPath(PathSet* set, Path* p);

      void
      RemovePathBuilder(Builder* ctx);

      void
      RemovePathSet(PathSet* set);

      using TransitHopsMap_t =
          std::multimap< PathID_t, std::shared_ptr< TransitHop > >;

      struct SyncTransitMap_t
      {
        util::Mutex first;  // protects second
        TransitHopsMap_t second GUARDED_BY(first);
      };

      // maps path id -> pathset owner of path
      using OwnedPathsMap_t = std::map< PathID_t, PathSet* >;

      struct SyncOwnedPathsMap_t
      {
        util::Mutex first;  // protects second
        OwnedPathsMap_t second GUARDED_BY(first);
      };

      llarp_threadpool*
      Worker();

      llarp::Crypto*
      Crypto();

      llarp::Logic*
      Logic();

      AbstractRouter*
      Router();

      const llarp::SecretKey&
      EncryptionSecretKey();

      const byte_t*
      OurRouterID() const;

     private:
      AbstractRouter* m_Router;
      SyncTransitMap_t m_TransitPaths;
      SyncTransitMap_t m_Paths;
      SyncOwnedPathsMap_t m_OurPaths;
      std::list< Builder* > m_PathBuilders;
      bool m_AllowTransit;
    };
  }  // namespace path
}  // namespace llarp

#endif
