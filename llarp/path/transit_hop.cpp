#include <path/path.hpp>

#include <dht/context.hpp>
#include <exit/context.hpp>
#include <messages/discard.hpp>
#include <messages/exit.hpp>
#include <messages/path_latency.hpp>
#include <messages/path_transfer.hpp>
#include <messages/relay_commit.hpp>
#include <router/abstractrouter.hpp>
#include <routing/handler.hpp>
#include <util/buffer.hpp>
#include <util/endian.hpp>

namespace llarp
{
  namespace path
  {
    TransitHop::TransitHop()
    {
    }

    bool
    TransitHop::Expired(llarp_time_t now) const
    {
      return now >= ExpireTime();
    }

    llarp_time_t
    TransitHop::ExpireTime() const
    {
      return started + lifetime;
    }

    TransitHopInfo::TransitHopInfo(const TransitHopInfo& other)
        : txID(other.txID)
        , rxID(other.rxID)
        , upstream(other.upstream)
        , downstream(other.downstream)
    {
    }

    TransitHopInfo::TransitHopInfo(const RouterID& down,
                                   const LR_CommitRecord& record)
        : txID(record.txid)
        , rxID(record.rxid)
        , upstream(record.nextHop)
        , downstream(down)
    {
    }

    TransitHop::TransitHop(const TransitHop& other)
        : info(other.info)
        , pathKey(other.pathKey)
        , started(other.started)
        , lifetime(other.lifetime)
        , version(other.version)
    {
    }

    bool
    TransitHop::SendRoutingMessage(const llarp::routing::IMessage* msg,
                                   AbstractRouter* r)
    {
      if(!IsEndpoint(r->pubkey()))
        return false;

      std::array< byte_t, MAX_LINK_MSG_SIZE - 128 > tmp;
      llarp_buffer_t buf(tmp);
      if(!msg->BEncode(&buf))
      {
        llarp::LogError("failed to encode routing message");
        return false;
      }
      TunnelNonce N;
      N.Randomize();
      buf.sz = buf.cur - buf.base;
      // pad to nearest MESSAGE_PAD_SIZE bytes
      auto dlt = buf.sz % pad_size;
      if(dlt)
      {
        dlt = pad_size - dlt;
        // randomize padding
        r->crypto()->randbytes(buf.cur, dlt);
        buf.sz += dlt;
      }
      buf.cur = buf.base;
      return HandleDownstream(buf, N, r);
    }

    bool
    TransitHop::HandleDownstream(const llarp_buffer_t& buf,
                                 const TunnelNonce& Y, AbstractRouter* r)
    {
      RelayDownstreamMessage msg;
      msg.pathid = info.rxID;
      msg.Y      = Y ^ nonceXOR;
      r->crypto()->xchacha20(buf, pathKey, Y);
      msg.X = buf;
      llarp::LogDebug("relay ", msg.X.size(), " bytes downstream from ",
                      info.upstream, " to ", info.downstream);
      return r->SendToOrQueue(info.downstream, &msg);
    }

    bool
    TransitHop::HandleUpstream(const llarp_buffer_t& buf, const TunnelNonce& Y,
                               AbstractRouter* r)
    {
      r->crypto()->xchacha20(buf, pathKey, Y);
      if(IsEndpoint(r->pubkey()))
      {
        m_LastActivity = r->Now();
        return r->ParseRoutingMessageBuffer(buf, this, info.rxID);
      }
      else
      {
        RelayUpstreamMessage msg;
        msg.pathid = info.txID;
        msg.Y      = Y ^ nonceXOR;

        msg.X = buf;
        llarp::LogDebug("relay ", msg.X.size(), " bytes upstream from ",
                        info.downstream, " to ", info.upstream);
        return r->SendToOrQueue(info.upstream, &msg);
      }
    }

    bool
    TransitHop::HandleDHTMessage(const llarp::dht::IMessage* msg,
                                 AbstractRouter* r)
    {
      return r->dht()->impl->RelayRequestForPath(info.rxID, msg);
    }

    bool
    TransitHop::HandlePathLatencyMessage(
        const llarp::routing::PathLatencyMessage* msg, AbstractRouter* r)
    {
      llarp::routing::PathLatencyMessage reply;
      reply.L = msg->T;
      return SendRoutingMessage(&reply, r);
    }

    bool
    TransitHop::HandlePathConfirmMessage(
        __attribute__((unused)) const llarp::routing::PathConfirmMessage* msg,
        __attribute__((unused)) AbstractRouter* r)
    {
      llarp::LogWarn("unwarranted path confirm message on ", info);
      return false;
    }

    bool
    TransitHop::HandleDataDiscardMessage(
        __attribute__((unused)) const llarp::routing::DataDiscardMessage* msg,
        __attribute__((unused)) AbstractRouter* r)
    {
      llarp::LogWarn("unwarranted path data discard message on ", info);
      return false;
    }

    bool
    TransitHop::HandleObtainExitMessage(
        const llarp::routing::ObtainExitMessage* msg, AbstractRouter* r)
    {
      if(msg->Verify(r->crypto())
         && r->exitContext().ObtainNewExit(msg->I, info.rxID, msg->E != 0))
      {
        llarp::routing::GrantExitMessage grant;
        grant.S = NextSeqNo();
        grant.T = msg->T;
        if(!grant.Sign(r->crypto(), r->identity()))
        {
          llarp::LogError("Failed to sign grant exit message");
          return false;
        }
        return SendRoutingMessage(&grant, r);
      }
      // TODO: exponential backoff
      // TODO: rejected policies
      llarp::routing::RejectExitMessage reject;
      reject.S = NextSeqNo();
      reject.T = msg->T;
      if(!reject.Sign(r->crypto(), r->identity()))
      {
        llarp::LogError("Failed to sign reject exit message");
        return false;
      }
      return SendRoutingMessage(&reject, r);
    }

    bool
    TransitHop::HandleCloseExitMessage(
        const llarp::routing::CloseExitMessage* msg, AbstractRouter* r)
    {
      llarp::routing::DataDiscardMessage discard(info.rxID, msg->S);
      auto ep = r->exitContext().FindEndpointForPath(info.rxID);
      if(ep && msg->Verify(r->crypto(), ep->PubKey()))
      {
        ep->Close();
        // ep is now gone af
        llarp::routing::CloseExitMessage reply;
        reply.S = NextSeqNo();
        if(reply.Sign(r->crypto(), r->identity()))
          return SendRoutingMessage(&reply, r);
      }
      return SendRoutingMessage(&discard, r);
    }

    bool
    TransitHop::HandleUpdateExitVerifyMessage(
        const llarp::routing::UpdateExitVerifyMessage* msg, AbstractRouter* r)
    {
      (void)msg;
      (void)r;
      llarp::LogError("unwarranted exit verify on ", info);
      return false;
    }

    bool
    TransitHop::HandleUpdateExitMessage(
        const llarp::routing::UpdateExitMessage* msg, AbstractRouter* r)
    {
      auto ep = r->exitContext().FindEndpointForPath(msg->P);
      if(ep)
      {
        if(!msg->Verify(r->crypto(), ep->PubKey()))
          return false;

        if(ep->UpdateLocalPath(info.rxID))
        {
          llarp::routing::UpdateExitVerifyMessage reply;
          reply.T = msg->T;
          reply.S = NextSeqNo();
          return SendRoutingMessage(&reply, r);
        }
      }
      // on fail tell message was discarded
      llarp::routing::DataDiscardMessage discard(info.rxID, msg->S);
      return SendRoutingMessage(&discard, r);
    }

    bool
    TransitHop::HandleRejectExitMessage(
        const llarp::routing::RejectExitMessage* msg, AbstractRouter* r)
    {
      (void)msg;
      (void)r;
      llarp::LogError(info, " got unwarranted RXM");
      return false;
    }

    bool
    TransitHop::HandleGrantExitMessage(
        const llarp::routing::GrantExitMessage* msg, AbstractRouter* r)
    {
      (void)msg;
      (void)r;
      llarp::LogError(info, " got unwarranted GXM");
      return false;
    }

    bool
    TransitHop::HandleTransferTrafficMessage(
        const llarp::routing::TransferTrafficMessage* msg, AbstractRouter* r)
    {
      auto endpoint = r->exitContext().FindEndpointForPath(info.rxID);
      if(endpoint)
      {
        bool sent = true;
        for(const auto& pkt : msg->X)
        {
          // check short packet buffer
          if(pkt.size() <= 8)
            continue;
          uint64_t counter = bufbe64toh(pkt.data());
          sent &= endpoint->QueueOutboundTraffic(
              ManagedBuffer(llarp_buffer_t(pkt.data() + 8, pkt.size() - 8)),
              counter);
        }
        return sent;
      }
      else
        llarp::LogError("No exit endpoint on ", info);
      // discarded
      llarp::routing::DataDiscardMessage discard(info.rxID, msg->S);
      return SendRoutingMessage(&discard, r);
    }

    bool
    TransitHop::HandlePathTransferMessage(
        const llarp::routing::PathTransferMessage* msg, AbstractRouter* r)
    {
      auto path = r->pathContext().GetPathForTransfer(msg->P);
      llarp::routing::DataDiscardMessage discarded(msg->P, msg->S);
      if(path == nullptr || msg->T.F != info.txID)
      {
        return SendRoutingMessage(&discarded, r);
      }

      std::array< byte_t, service::MAX_PROTOCOL_MESSAGE_SIZE > tmp;
      llarp_buffer_t buf(tmp);
      if(!msg->T.BEncode(&buf))
      {
        llarp::LogWarn(info, " failed to transfer data message, encode failed");
        return SendRoutingMessage(&discarded, r);
      }
      // rewind
      buf.sz  = buf.cur - buf.base;
      buf.cur = buf.base;
      // send
      if(path->HandleDownstream(buf, msg->Y, r))
        return true;
      return SendRoutingMessage(&discarded, r);
    }

  }  // namespace path
}  // namespace llarp
