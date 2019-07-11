#include <path/path.hpp>

#include <exit/exit_messages.hpp>
#include <messages/discard.hpp>
#include <messages/relay_commit.hpp>
#include <path/pathbuilder.hpp>
#include <path/transit_hop.hpp>
#include <profiling.hpp>
#include <router/abstractrouter.hpp>
#include <routing/dht_message.hpp>
#include <routing/path_latency_message.hpp>
#include <routing/transfer_traffic_message.hpp>
#include <util/buffer.hpp>
#include <util/endian.hpp>

#include <deque>

namespace llarp
{
  namespace path
  {
    Path::Path(const std::vector< RouterContact >& h, PathSet* parent,
               PathRole startingRoles)
        : m_PathSet(parent), _role(startingRoles)
    {
      hops.resize(h.size());
      size_t hsz = h.size();
      for(size_t idx = 0; idx < hsz; ++idx)
      {
        hops[idx].rc = h[idx];
        hops[idx].txID.Randomize();
        hops[idx].rxID.Randomize();
      }

      for(size_t idx = 0; idx < hsz - 1; ++idx)
      {
        hops[idx].txID = hops[idx + 1].rxID;
      }
      // initialize parts of the introduction
      intro.router = hops[hsz - 1].rc.pubkey;
      intro.pathID = hops[hsz - 1].txID;
      EnterState(ePathBuilding, parent->Now());
    }

    void
    Path::SetBuildResultHook(BuildResultHookFunc func)
    {
      m_BuiltHook = func;
    }

    RouterID
    Path::Endpoint() const
    {
      return hops[hops.size() - 1].rc.pubkey;
    }

    PubKey
    Path::EndpointPubKey() const
    {
      return hops[hops.size() - 1].rc.pubkey;
    }

    PathID_t
    Path::TXID() const
    {
      return hops[0].txID;
    }

    PathID_t
    Path::RXID() const
    {
      return hops[0].rxID;
    }

    bool
    Path::IsReady() const
    {
      return intro.latency > 0 && _status == ePathEstablished;
    }

    bool
    Path::IsEndpoint(const RouterID& r, const PathID_t& id) const
    {
      return hops[hops.size() - 1].rc.pubkey == r
          && hops[hops.size() - 1].txID == id;
    }

    RouterID
    Path::Upstream() const
    {
      return hops[0].rc.pubkey;
    }

    std::string
    Path::HopsString() const
    {
      std::stringstream ss;
      for(const auto& hop : hops)
        ss << RouterID(hop.rc.pubkey) << " -> ";
      return ss.str();
    }

    void
    Path::EnterState(PathStatus st, llarp_time_t now)
    {
      if(st == ePathExpired && _status == ePathBuilding)
      {
        _status = st;
        m_PathSet->HandlePathBuildTimeout(shared_from_this());
      }
      else if(st == ePathBuilding)
      {
        LogInfo("path ", Name(), " is building");
        buildStarted = now;
      }
      else if(st == ePathEstablished && _status == ePathBuilding)
      {
        LogInfo("path ", Name(), " is built, took ", now - buildStarted, " ms");
      }
      else if(st == ePathTimeout && _status == ePathEstablished)
      {
        LogInfo("path ", Name(), " died");
        _status = st;
        m_PathSet->HandlePathDied(shared_from_this());
      }
      else if(st == ePathEstablished && _status == ePathTimeout)
      {
        LogInfo("path ", Name(), " reanimated");
      }
      _status = st;
    }

    util::StatusObject
    PathHopConfig::ExtractStatus() const
    {
      util::StatusObject obj{{"lifetime", lifetime},
                             {"router", rc.pubkey.ToHex()},
                             {"txid", txID.ToHex()},
                             {"rxid", rxID.ToHex()}};
      return obj;
    }

    util::StatusObject
    Path::ExtractStatus() const
    {
      auto now = llarp::time_now_ms();

      util::StatusObject obj{{"intro", intro.ExtractStatus()},
                             {"lastRecvMsg", m_LastRecvMessage},
                             {"lastLatencyTest", m_LastLatencyTestTime},
                             {"buildStarted", buildStarted},
                             {"expired", Expired(now)},
                             {"expiresSoon", ExpiresSoon(now)},
                             {"expiresAt", ExpireTime()},
                             {"ready", IsReady()},
                             {"hasExit", SupportsAnyRoles(ePathRoleExit)}};

      std::vector< util::StatusObject > hopsObj;
      std::transform(hops.begin(), hops.end(), std::back_inserter(hopsObj),
                     [](const auto& hop) -> util::StatusObject {
                       return hop.ExtractStatus();
                     });
      obj.Put("hops", hopsObj);

      switch(_status)
      {
        case ePathBuilding:
          obj.Put("status", "building");
          break;
        case ePathEstablished:
          obj.Put("status", "established");
          break;
        case ePathTimeout:
          obj.Put("status", "timeout");
          break;
        case ePathExpired:
          obj.Put("status", "expired");
          break;
        case ePathIgnore:
          obj.Put("status", "ignored");
        default:
          obj.Put("status", "unknown");
          break;
      }
      return obj;
    }

    void
    Path::Rebuild()
    {
      std::vector< RouterContact > newHops;
      for(const auto& hop : hops)
        newHops.emplace_back(hop.rc);
      LogInfo(Name(), " rebuilding on ", HopsString());
      m_PathSet->Build(newHops);
    }

    void
    Path::Tick(llarp_time_t now, AbstractRouter* r)
    {
      if(Expired(now))
        return;

      if(_status == ePathBuilding)
      {
        if(now >= buildStarted)
        {
          auto dlt = now - buildStarted;
          if(dlt >= path::build_timeout)
          {
            r->routerProfiling().MarkPathFail(this);
            EnterState(ePathExpired, now);
            return;
          }
        }
      }
      // check to see if this path is dead
      if(_status == ePathEstablished)
      {
        const auto dlt = now - m_LastLatencyTestTime;
        if(dlt > path::latency_interval && m_LastLatencyTestID == 0)
        {
          routing::PathLatencyMessage latency;
          latency.T             = randint();
          m_LastLatencyTestID   = latency.T;
          m_LastLatencyTestTime = now;
          SendRoutingMessage(latency, r);
          return;
        }
        if(m_LastRecvMessage && now > m_LastRecvMessage)
        {
          const auto delay = now - m_LastRecvMessage;
          if(m_CheckForDead && m_CheckForDead(shared_from_this(), delay))
          {
            r->routerProfiling().MarkPathFail(this);
            EnterState(ePathTimeout, now);
          }
        }
        else if(dlt >= path::alive_timeout && m_LastRecvMessage == 0)
        {
          if(m_CheckForDead && m_CheckForDead(shared_from_this(), dlt))
          {
            r->routerProfiling().MarkPathFail(this);
            EnterState(ePathTimeout, now);
          }
        }
      }
    }

    bool
    Path::HandleUpstream(const llarp_buffer_t& buf, const TunnelNonce& Y,
                         AbstractRouter* r)
    {
      TunnelNonce n = Y;
      for(const auto& hop : hops)
      {
        CryptoManager::instance()->xchacha20(buf, hop.shared, n);
        n ^= hop.nonceXOR;
      }
      RelayUpstreamMessage msg;
      msg.X      = buf;
      msg.Y      = Y;
      msg.pathid = TXID();
      if(r->SendToOrQueue(Upstream(), &msg))
        return true;
      LogError("send to ", Upstream(), " failed");
      return false;
    }

    bool
    Path::Expired(llarp_time_t now) const
    {
      if(_status == ePathEstablished || _status == ePathTimeout)
        return now >= ExpireTime();
      if(_status == ePathBuilding)
        return false;

      return true;
    }

    std::string
    Path::Name() const
    {
      std::stringstream ss;
      ss << "TX=" << TXID() << " RX=" << RXID();
      if(m_PathSet)
        ss << " on " << m_PathSet->Name();
      return ss.str();
    }

    bool
    Path::HandleDownstream(const llarp_buffer_t& buf, const TunnelNonce& Y,
                           AbstractRouter* r)
    {
      TunnelNonce n = Y;
      for(const auto& hop : hops)
      {
        n ^= hop.nonceXOR;
        CryptoManager::instance()->xchacha20(buf, hop.shared, n);
      }
      if(!HandleRoutingMessage(buf, r))
        return false;
      m_LastRecvMessage = r->Now();
      return true;
    }

    bool
    Path::HandleRoutingMessage(const llarp_buffer_t& buf, AbstractRouter* r)
    {
      if(!r->ParseRoutingMessageBuffer(buf, this, RXID()))
      {
        LogWarn("Failed to parse inbound routing message");
        return false;
      }
      return true;
    }

    bool
    Path::HandleUpdateExitVerifyMessage(
        const routing::UpdateExitVerifyMessage& msg, AbstractRouter* r)
    {
      (void)r;
      if(m_UpdateExitTX && msg.T == m_UpdateExitTX)
      {
        if(m_ExitUpdated)
          return m_ExitUpdated(shared_from_this());
      }
      if(m_CloseExitTX && msg.T == m_CloseExitTX)
      {
        if(m_ExitClosed)
          return m_ExitClosed(shared_from_this());
      }
      return false;
    }

    bool
    Path::SendRoutingMessage(const routing::IMessage& msg, AbstractRouter* r)
    {
      std::array< byte_t, MAX_LINK_MSG_SIZE / 2 > tmp;
      llarp_buffer_t buf(tmp);
      // should help prevent bad paths with uninitialized members
      // FIXME: Why would we get uninitialized IMessages?
      if(msg.version != LLARP_PROTO_VERSION)
        return false;
      if(!msg.BEncode(&buf))
      {
        LogError("Bencode failed");
        DumpBuffer(buf);
        return false;
      }
      // make nonce
      TunnelNonce N;
      N.Randomize();
      buf.sz = buf.cur - buf.base;
      // pad smaller messages
      if(buf.sz < pad_size)
      {
        // randomize padding
        CryptoManager::instance()->randbytes(buf.cur, pad_size - buf.sz);
        buf.sz = pad_size;
      }
      buf.cur = buf.base;
      return HandleUpstream(buf, N, r);
    }

    bool
    Path::HandlePathTransferMessage(
        ABSL_ATTRIBUTE_UNUSED const routing::PathTransferMessage& msg,
        ABSL_ATTRIBUTE_UNUSED AbstractRouter* r)
    {
      LogWarn("unwarranted path transfer message on tx=", TXID(),
              " rx=", RXID());
      return false;
    }

    bool
    Path::HandleDataDiscardMessage(const routing::DataDiscardMessage& msg,
                                   AbstractRouter* r)
    {
      MarkActive(r->Now());
      if(m_DropHandler)
        return m_DropHandler(shared_from_this(), msg.P, msg.S);
      return true;
    }

    bool
    Path::HandlePathConfirmMessage(
        ABSL_ATTRIBUTE_UNUSED const routing::PathConfirmMessage& msg,
        AbstractRouter* r)
    {
      auto now = r->Now();
      if(_status == ePathBuilding)
      {
        // finish initializing introduction
        intro.expiresAt = buildStarted + hops[0].lifetime;

        r->routerProfiling().MarkPathSuccess(this);

        // persist session with upstream router until the path is done
        r->PersistSessionUntil(Upstream(), intro.expiresAt);
        MarkActive(now);
        // send path latency test
        routing::PathLatencyMessage latency;
        latency.T             = randint();
        m_LastLatencyTestID   = latency.T;
        m_LastLatencyTestTime = now;
        return SendRoutingMessage(latency, r);
      }
      LogWarn("got unwarranted path confirm message on tx=", RXID(),
              " rx=", RXID());
      return false;
    }

    bool
    Path::HandleHiddenServiceFrame(const service::ProtocolFrame& frame)
    {
      MarkActive(m_PathSet->Now());
      return m_DataHandler && m_DataHandler(shared_from_this(), frame);
    }

    bool
    Path::HandlePathLatencyMessage(const routing::PathLatencyMessage& msg,
                                   AbstractRouter* r)
    {
      auto now = r->Now();
      MarkActive(now);
      if(msg.L == m_LastLatencyTestID)
      {
        intro.latency       = now - m_LastLatencyTestTime;
        m_LastLatencyTestID = 0;
        EnterState(ePathEstablished, now);
        if(m_BuiltHook)
          m_BuiltHook(shared_from_this());
        m_BuiltHook = nullptr;
        LogDebug("path latency is now ", intro.latency, " for ", Name());
        return true;
      }

      LogWarn("unwarranted path latency message via ", Upstream());
      return false;
    }

    bool
    Path::HandleDHTMessage(const dht::IMessage& msg, AbstractRouter* r)
    {
      MarkActive(r->Now());
      routing::DHTMessage reply;
      if(!msg.HandleMessage(r->dht(), reply.M))
        return false;
      if(reply.M.size())
        return SendRoutingMessage(reply, r);
      return true;
    }

    bool
    Path::HandleCloseExitMessage(const routing::CloseExitMessage& msg,
                                 ABSL_ATTRIBUTE_UNUSED AbstractRouter* r)
    {
      /// allows exits to close from their end
      if(SupportsAnyRoles(ePathRoleExit | ePathRoleSVC))
      {
        if(msg.Verify(EndpointPubKey()))
        {
          LogInfo(Name(), " had its exit closed");
          _role &= ~ePathRoleExit;
          return true;
        }

        LogError(Name(), " CXM from exit with bad signature");
      }
      else
        LogError(Name(), " unwarranted CXM");
      return false;
    }

    bool
    Path::SendExitRequest(const routing::ObtainExitMessage& msg,
                          AbstractRouter* r)
    {
      LogInfo(Name(), " sending exit request to ", Endpoint());
      m_ExitObtainTX = msg.T;
      return SendRoutingMessage(msg, r);
    }

    bool
    Path::SendExitClose(const routing::CloseExitMessage& msg, AbstractRouter* r)
    {
      LogInfo(Name(), " closing exit to ", Endpoint());
      // mark as not exit anymore
      _role &= ~ePathRoleExit;
      return SendRoutingMessage(msg, r);
    }

    bool
    Path::HandleObtainExitMessage(const routing::ObtainExitMessage& msg,
                                  AbstractRouter* r)
    {
      (void)msg;
      (void)r;
      LogError(Name(), " got unwarranted OXM");
      return false;
    }

    bool
    Path::HandleUpdateExitMessage(const routing::UpdateExitMessage& msg,
                                  AbstractRouter* r)
    {
      (void)msg;
      (void)r;
      LogError(Name(), " got unwarranted UXM");
      return false;
    }

    bool
    Path::HandleRejectExitMessage(const routing::RejectExitMessage& msg,
                                  AbstractRouter* r)
    {
      if(m_ExitObtainTX && msg.T == m_ExitObtainTX)
      {
        if(!msg.Verify(EndpointPubKey()))
        {
          LogError(Name(), "RXM invalid signature");
          return false;
        }
        LogInfo(Name(), " ", Endpoint(), " Rejected exit");
        MarkActive(r->Now());
        return InformExitResult(msg.B);
      }
      LogError(Name(), " got unwarranted RXM");
      return false;
    }

    bool
    Path::HandleGrantExitMessage(const routing::GrantExitMessage& msg,
                                 AbstractRouter* r)
    {
      if(m_ExitObtainTX && msg.T == m_ExitObtainTX)
      {
        if(!msg.Verify(EndpointPubKey()))
        {
          LogError(Name(), " GXM signature failed");
          return false;
        }
        // we now can send exit traffic
        _role |= ePathRoleExit;
        LogInfo(Name(), " ", Endpoint(), " Granted exit");
        MarkActive(r->Now());
        return InformExitResult(0);
      }
      LogError(Name(), " got unwarranted GXM");
      return false;
    }

    bool
    Path::InformExitResult(llarp_time_t B)
    {
      auto self   = shared_from_this();
      bool result = true;
      for(const auto& hook : m_ObtainedExitHooks)
        result &= hook(self, B);
      m_ObtainedExitHooks.clear();
      return result;
    }

    bool
    Path::HandleTransferTrafficMessage(
        const routing::TransferTrafficMessage& msg, AbstractRouter* r)
    {
      // check if we can handle exit data
      if(!SupportsAnyRoles(ePathRoleExit | ePathRoleSVC))
        return false;
      // handle traffic if we have a handler
      if(!m_ExitTrafficHandler)
        return false;
      bool sent = msg.X.size() > 0;
      auto self = shared_from_this();
      for(const auto& pkt : msg.X)
      {
        if(pkt.size() <= 8)
          return false;
        uint64_t counter = bufbe64toh(pkt.data());
        if(m_ExitTrafficHandler(
               self, llarp_buffer_t(pkt.data() + 8, pkt.size() - 8), counter))
        {
          MarkActive(r->Now());
          EnterState(ePathEstablished, r->Now());
        }
      }
      return sent;
    }

  }  // namespace path
}  // namespace llarp
