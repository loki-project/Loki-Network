#include <service/outbound_context.hpp>

#include <router/abstractrouter.hpp>
#include <service/async_key_exchange.hpp>
#include <service/hidden_service_address_lookup.hpp>
#include <service/endpoint.hpp>
#include <nodedb.hpp>
#include <profiling.hpp>

namespace llarp
{
  namespace service
  {
    bool
    OutboundContext::Stop()
    {
      markedBad = true;
      return path::Builder::Stop();
    }

    bool
    OutboundContext::IsDone(llarp_time_t now) const
    {
      (void)now;
      return AvailablePaths(path::ePathRoleAny) == 0 && ShouldRemove();
    }

    bool
    OutboundContext::ShouldBundleRC() const
    {
      return m_Endpoint->ShouldBundleRC();
    }

    bool
    OutboundContext::HandleDataDrop(path::Path* p, const PathID_t& dst,
                                    uint64_t seq)
    {
      // pick another intro
      if(dst == remoteIntro.pathID && remoteIntro.router == p->Endpoint())
      {
        LogWarn(Name(), " message ", seq, " dropped by endpoint ",
                p->Endpoint(), " via ", dst);
        if(MarkCurrentIntroBad(Now()))
        {
          LogInfo(Name(), " switched intros to ", remoteIntro.router, " via ",
                  remoteIntro.pathID);
        }
        UpdateIntroSet(true);
      }
      return true;
    }

    OutboundContext::OutboundContext(const IntroSet& introset, Endpoint* parent)
        : path::Builder(parent->Router(), parent->Router()->dht(), 3,
                        path::default_len)
        , SendContext(introset.A, {}, this, parent)
        , currentIntroSet(introset)

    {
      updatingIntroSet = false;
      for(const auto intro : introset.I)
      {
        if(intro.expiresAt > m_NextIntro.expiresAt)
          m_NextIntro = intro;
      }
    }

    OutboundContext::~OutboundContext()
    {
    }

    /// actually swap intros
    void
    OutboundContext::SwapIntros()
    {
      remoteIntro = m_NextIntro;
      m_DataHandler->PutIntroFor(currentConvoTag, remoteIntro);
    }

    bool
    OutboundContext::OnIntroSetUpdate(__attribute__((unused))
                                      const Address& addr,
                                      const IntroSet* i,
                                      const RouterID& endpoint)
    {
      if(markedBad)
        return true;
      updatingIntroSet = false;
      if(i)
      {
        if(currentIntroSet.T >= i->T)
        {
          LogInfo("introset is old, dropping");
          return true;
        }
        auto now = Now();
        if(i->IsExpired(now))
        {
          LogError("got expired introset from lookup from ", endpoint);
          return true;
        }
        currentIntroSet = *i;
        if(!ShiftIntroduction())
        {
          LogWarn("failed to pick new intro during introset update");
        }
        if(GetPathByRouter(m_NextIntro.router) == nullptr)
          BuildOneAlignedTo(m_NextIntro.router);
        else
          SwapIntros();
      }
      else
        ++m_LookupFails;
      return true;
    }

    bool
    OutboundContext::ReadyToSend() const
    {
      return (!remoteIntro.router.IsZero())
          && GetPathByRouter(remoteIntro.router) != nullptr;
    }

    void
    OutboundContext::HandlePathBuilt(path::Path* p)
    {
      path::Builder::HandlePathBuilt(p);
      /// don't use it if we are marked bad
      if(markedBad)
        return;
      p->SetDataHandler(std::bind(&OutboundContext::HandleHiddenServiceFrame,
                                  this, std::placeholders::_1,
                                  std::placeholders::_2));
      p->SetDropHandler(std::bind(&OutboundContext::HandleDataDrop, this,
                                  std::placeholders::_1, std::placeholders::_2,
                                  std::placeholders::_3));
    }

    bool
    OutboundContext::BuildOneAlignedTo(const RouterID& remote)
    {
      LogInfo(Name(), " building path to ", remote);
      auto nodedb = m_Endpoint->Router()->nodedb();
      std::vector< RouterContact > hops;
      hops.resize(numHops);
      for(size_t hop = 0; hop < numHops; ++hop)
      {
        if(hop == 0)
        {
          if(!SelectHop(nodedb, hops[0], hops[0], 0, path::ePathRoleAny))
            return false;
        }
        else if(hop == numHops - 1)
        {
          // last hop
          if(!nodedb->Get(remote, hops[hop]))
            return false;
        }
        // middle hop
        else
        {
          size_t tries = 5;
          do
          {
            nodedb->select_random_hop_excluding(hops[hop],
                                                {hops[hop - 1].pubkey, remote});
            --tries;
          } while(m_Endpoint->Router()->routerProfiling().IsBadForPath(
                      hops[hop].pubkey)
                  && tries > 0);
          return tries > 0;
        }
        return false;
      }
      Build(hops);
      return true;
    }

    void
    OutboundContext::AsyncGenIntro(const llarp_buffer_t& payload,
                                   __attribute__((unused)) ProtocolType t)
    {
      auto path = m_PathSet->GetPathByRouter(remoteIntro.router);
      if(path == nullptr)
      {
        // try parent as fallback
        path = m_Endpoint->GetPathByRouter(remoteIntro.router);
        if(path == nullptr)
        {
          BuildOneAlignedTo(remoteIntro.router);
          LogWarn(Name(), " dropping intro frame, no path to ",
                  remoteIntro.router);
          return;
        }
      }
      currentConvoTag.Randomize();
      AsyncKeyExchange* ex = new AsyncKeyExchange(
          m_Endpoint->RouterLogic(), m_Endpoint->Crypto(), remoteIdent,
          m_Endpoint->GetIdentity(), currentIntroSet.K, remoteIntro,
          m_DataHandler, currentConvoTag);

      ex->hook = std::bind(&OutboundContext::Send, this, std::placeholders::_1);

      ex->msg.PutBuffer(payload);
      ex->msg.introReply = path->intro;
      ex->frame.F        = ex->msg.introReply.pathID;
      llarp_threadpool_queue_job(m_Endpoint->Worker(),
                                 {ex, &AsyncKeyExchange::Encrypt});
    }

    std::string
    OutboundContext::Name() const
    {
      return "OBContext:" + m_Endpoint->Name() + "-"
          + currentIntroSet.A.Addr().ToString();
    }

    void
    OutboundContext::UpdateIntroSet(bool randomizePath)
    {
      if(updatingIntroSet || markedBad)
        return;
      auto addr = currentIntroSet.A.Addr();

      path::Path* path = nullptr;
      if(randomizePath)
        path = m_Endpoint->PickRandomEstablishedPath();
      else
        path = m_Endpoint->GetEstablishedPathClosestTo(addr.as_array());

      if(path)
      {
        HiddenServiceAddressLookup* job = new HiddenServiceAddressLookup(
            m_Endpoint,
            std::bind(&OutboundContext::OnIntroSetUpdate, this,
                      std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3),
            addr, m_Endpoint->GenTXID());

        updatingIntroSet = job->SendRequestViaPath(path, m_Endpoint->Router());
      }
      else
      {
        LogWarn("Cannot update introset no path for outbound session to ",
                currentIntroSet.A.Addr().ToString());
      }
    }

    util::StatusObject
    OutboundContext::ExtractStatus() const
    {
      auto obj = path::Builder::ExtractStatus();
      obj.Put("currentConvoTag", currentConvoTag.ToHex());
      obj.Put("remoteIntro", remoteIntro.ExtractStatus());
      obj.Put("sessionCreatedAt", createdAt);
      obj.Put("lastGoodSend", lastGoodSend);
      obj.Put("seqno", sequenceNo);
      obj.Put("markedBad", markedBad);
      obj.Put("lastShift", lastShift);
      obj.Put("remoteIdentity", remoteIdent.Addr().ToString());
      obj.Put("currentRemoteIntroset", currentIntroSet.ExtractStatus());
      obj.Put("nextIntro", m_NextIntro.ExtractStatus());
      std::vector< util::StatusObject > badIntrosObj;
      std::transform(m_BadIntros.begin(), m_BadIntros.end(),
                     std::back_inserter(badIntrosObj),
                     [](const auto& item) -> util::StatusObject {
                       util::StatusObject o{
                           {"count", item.second},
                           {"intro", item.first.ExtractStatus()}};
                       return o;
                     });
      obj.Put("badIntros", badIntrosObj);
      return obj;
    }

    bool
    OutboundContext::Tick(llarp_time_t now)
    {
      // we are probably dead af
      if(m_LookupFails > 16 || m_BuildFails > 10)
        return true;
      // check for expiration
      if(remoteIntro.ExpiresSoon(now))
      {
        // shift intro if it expires "soon"
        ShiftIntroduction();
      }
      // swap if we can
      if(remoteIntro != m_NextIntro)
      {
        if(GetPathByRouter(m_NextIntro.router) != nullptr)
        {
          // we can safely set remoteIntro to the next one
          SwapIntros();
          LogInfo(Name(), " swapped intro");
        }
      }
      // lookup router in intro if set and unknown
      if(!remoteIntro.router.IsZero())
        m_Endpoint->EnsureRouterIsKnown(remoteIntro.router);
      // expire bad intros
      auto itr = m_BadIntros.begin();
      while(itr != m_BadIntros.end())
      {
        if(now - itr->second > path::default_lifetime)
          itr = m_BadIntros.erase(itr);
        else
          ++itr;
      }
      // send control message if we look too quiet
      if(lastGoodSend)
      {
        if(now - lastGoodSend > (sendTimeout / 2))
        {
          if(!GetNewestPathByRouter(remoteIntro.router))
          {
            BuildOneAlignedTo(remoteIntro.router);
          }
          Encrypted< 64 > tmp;
          tmp.Randomize();
          llarp_buffer_t buf(tmp.data(), tmp.size());
          AsyncEncryptAndSendTo(buf, eProtocolControl);
          SharedSecret k;
          if(currentConvoTag.IsZero())
            return false;
          return !m_DataHandler->HasConvoTag(currentConvoTag);
        }
      }
      // if we are dead return true so we are removed
      return lastGoodSend
          ? (now >= lastGoodSend && now - lastGoodSend > sendTimeout)
          : (now >= createdAt && now - createdAt > connectTimeout);
    }

    bool
    OutboundContext::SelectHop(llarp_nodedb* db, const RouterContact& prev,
                               RouterContact& cur, size_t hop,
                               path::PathRole roles)
    {
      if(remoteIntro.router.IsZero())
      {
        SwapIntros();
      }
      if(hop == numHops - 1)
      {
        m_Endpoint->EnsureRouterIsKnown(remoteIntro.router);
        if(db->Get(remoteIntro.router, cur))
          return true;
        ++m_BuildFails;
        return false;
      }
      else if(hop == numHops - 2)
      {
        return db->select_random_hop_excluding(
            cur, {prev.pubkey, remoteIntro.router});
      }
      return path::Builder::SelectHop(db, prev, cur, hop, roles);
    }

    bool
    OutboundContext::ShouldBuildMore(llarp_time_t now) const
    {
      if(markedBad)
        return false;
      if(path::Builder::ShouldBuildMore(now))
        return true;
      return !ReadyToSend();
    }

    bool
    OutboundContext::MarkCurrentIntroBad(llarp_time_t now)
    {
      // insert bad intro
      m_BadIntros[remoteIntro] = now;
      // unconditional shift
      bool shiftedRouter = false;
      bool shiftedIntro  = false;
      // try same router
      for(const auto& intro : currentIntroSet.I)
      {
        if(intro.ExpiresSoon(now))
          continue;
        if(router->routerProfiling().IsBadForPath(intro.router))
          continue;
        auto itr = m_BadIntros.find(intro);
        if(itr == m_BadIntros.end() && intro.router == m_NextIntro.router)
        {
          shiftedIntro = true;
          m_NextIntro  = intro;
          break;
        }
      }
      if(!shiftedIntro)
      {
        // try any router
        for(const auto& intro : currentIntroSet.I)
        {
          if(intro.ExpiresSoon(now))
            continue;
          auto itr = m_BadIntros.find(intro);
          if(itr == m_BadIntros.end())
          {
            // TODO: this should always be true but idk if it really is
            shiftedRouter = m_NextIntro.router != intro.router;
            shiftedIntro  = true;
            m_NextIntro   = intro;
            break;
          }
        }
      }
      if(shiftedRouter)
      {
        lastShift = now;
        BuildOneAlignedTo(m_NextIntro.router);
      }
      else if(shiftedIntro)
      {
        SwapIntros();
      }
      else
      {
        LogInfo(Name(), " updating introset");
        UpdateIntroSet(true);
      }
      return shiftedIntro;
    }

    bool
    OutboundContext::ShiftIntroduction(bool rebuild)
    {
      bool success = false;
      auto now     = Now();
      if(now - lastShift < MIN_SHIFT_INTERVAL)
        return false;
      bool shifted = false;
      // to find a intro on the same router as before
      for(const auto& intro : currentIntroSet.I)
      {
        if(intro.ExpiresSoon(now))
          continue;
        if(m_BadIntros.find(intro) == m_BadIntros.end()
           && remoteIntro.router == intro.router)
        {
          m_NextIntro = intro;
          return true;
        }
      }
      for(const auto& intro : currentIntroSet.I)
      {
        m_Endpoint->EnsureRouterIsKnown(intro.router);
        if(intro.ExpiresSoon(now))
          continue;
        if(m_BadIntros.find(intro) == m_BadIntros.end() && m_NextIntro != intro)
        {
          shifted = intro.router != m_NextIntro.router
              || (now < intro.expiresAt
                  && intro.expiresAt - now
                      > 10 * 1000);  // TODO: hardcoded value
          m_NextIntro = intro;
          success     = true;
          break;
        }
      }
      if(shifted && rebuild)
      {
        lastShift = now;
        BuildOneAlignedTo(m_NextIntro.router);
      }
      return success;
    }

    void
    OutboundContext::HandlePathDied(path::Path* path)
    {
      // unconditionally update introset
      UpdateIntroSet(true);
      const RouterID endpoint(path->Endpoint());
      // if a path to our current intro died...
      if(endpoint == remoteIntro.router)
      {
        // figure out how many paths to this router we have
        size_t num = 0;
        ForEachPath([&](path::Path* p) {
          if(p->Endpoint() == endpoint && p->IsReady())
            ++num;
        });
        // if we have more than two then we are probably fine
        if(num > 2)
          return;
        // if we have one working one ...
        if(num == 1)
        {
          num = 0;
          ForEachPath([&](path::Path* p) {
            if(p->Endpoint() == endpoint)
              ++num;
          });
          // if we have 2 or more established or pending don't do anything
          if(num > 2)
            return;
          BuildOneAlignedTo(endpoint);
        }
        else if(num == 0)
        {
          // we have no paths to this router right now
          // hop off it
          Introduction picked;
          // get the latest intro that isn't on that endpoint
          for(const auto& intro : currentIntroSet.I)
          {
            if(intro.router == endpoint)
              continue;
            if(intro.expiresAt > picked.expiresAt)
              picked = intro;
          }
          // we got nothing
          if(picked.router.IsZero())
          {
            return;
          }
          m_NextIntro = picked;
          // check if we have a path to this router
          num = 0;
          ForEachPath([&](path::Path* p) {
            if(p->Endpoint() == m_NextIntro.router)
              ++num;
          });
          // build a path if one isn't already pending build or established
          if(num == 0)
            BuildOneAlignedTo(m_NextIntro.router);
          SwapIntros();
        }
      }
    }

    bool
    OutboundContext::HandleHiddenServiceFrame(path::Path* p,
                                              const ProtocolFrame& frame)
    {
      return m_Endpoint->HandleHiddenServiceFrame(p, frame);
    }

  }  // namespace service

}  // namespace llarp
