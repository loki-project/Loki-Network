#include <path/path_context.hpp>

#include <messages/relay_commit.hpp>
#include <path/path.hpp>
#include <router/abstractrouter.hpp>
#include <router/i_outbound_message_handler.hpp>

namespace llarp
{
  namespace path
  {
    PathContext::PathContext(AbstractRouter* router)
        : m_Router(router), m_AllowTransit(false)
    {
    }

    void
    PathContext::AllowTransit()
    {
      m_AllowTransit = true;
    }

    bool
    PathContext::AllowingTransit() const
    {
      return m_AllowTransit;
    }

    std::shared_ptr< thread::ThreadPool >
    PathContext::Worker()
    {
      return m_Router->threadpool();
    }

    std::shared_ptr< Logic >
    PathContext::logic()
    {
      return m_Router->logic();
    }

    const SecretKey&
    PathContext::EncryptionSecretKey()
    {
      return m_Router->encryption();
    }

    bool
    PathContext::HopIsUs(const RouterID& k) const
    {
      return std::equal(m_Router->pubkey(), m_Router->pubkey() + PUBKEYSIZE,
                        k.begin());
    }

    PathContext::EndpointPathPtrSet
    PathContext::FindOwnedPathsWithEndpoint(const RouterID& r)
    {
      EndpointPathPtrSet found;
      m_OurPaths.ForEach([&](const PathSet_ptr& set) {
        set->ForEachPath([&](const Path_ptr& p) {
          if(p->Endpoint() == r && p->IsReady())
            found.insert(p);
        });
      });
      return found;
    }

    bool
    PathContext::ForwardLRCM(const RouterID& nextHop,
                             const std::array< EncryptedFrame, 8 >& frames,
                             SendStatusHandler handler)
    {
      if(handler == nullptr)
      {
        LogError("Calling ForwardLRCM without passing result handler");
        return false;
      }

      auto msg = std::make_shared< const LR_CommitMessage >(frames);

      LogDebug("forwarding LRCM to ", nextHop);

      m_Router->SendToOrQueue(nextHop, msg.get(), handler);

      return true;
    }
    template < typename Lock_t, typename Map_t, typename Key_t,
               typename CheckValue_t, typename GetFunc_t >
    HopHandler_ptr
    MapGet(Map_t& map, const Key_t& k, CheckValue_t check, GetFunc_t get)
    {
      Lock_t lock(&map.first);
      auto range = map.second.equal_range(k);
      for(auto i = range.first; i != range.second; ++i)
      {
        if(check(i->second))
          return get(i->second);
      }
      return nullptr;
    }

    template < typename Lock_t, typename Map_t, typename Key_t,
               typename CheckValue_t >
    bool
    MapHas(Map_t& map, const Key_t& k, CheckValue_t check)
    {
      Lock_t lock(&map.first);
      auto range = map.second.equal_range(k);
      for(auto i = range.first; i != range.second; ++i)
      {
        if(check(i->second))
          return true;
      }
      return false;
    }

    template < typename Lock_t, typename Map_t, typename Key_t,
               typename Value_t >
    void
    MapPut(Map_t& map, const Key_t& k, const Value_t& v)
    {
      Lock_t lock(&map.first);
      map.second.emplace(k, v);
    }

    template < typename Lock_t, typename Map_t, typename Visit_t >
    void
    MapIter(Map_t& map, Visit_t v)
    {
      Lock_t lock(map.first);
      for(const auto& item : map.second)
        v(item);
    }

    template < typename Lock_t, typename Map_t, typename Key_t,
               typename Check_t >
    void
    MapDel(Map_t& map, const Key_t& k, Check_t check)
    {
      Lock_t lock(map.first);
      auto range = map.second.equal_range(k);
      for(auto i = range.first; i != range.second;)
      {
        if(check(i->second))
          i = map.second.erase(i);
        else
          ++i;
      }
    }

    void
    PathContext::AddOwnPath(PathSet_ptr set, Path_ptr path)
    {
      set->AddPath(path);
      MapPut< SyncOwnedPathsMap_t::Lock_t >(m_OurPaths, path->TXID(), set);
      MapPut< SyncOwnedPathsMap_t::Lock_t >(m_OurPaths, path->RXID(), set);
    }

    bool
    PathContext::HasTransitHop(const TransitHopInfo& info)
    {
      if(not m_AllowTransit)
        return false;
      return MapHas< SyncTransitMap_t::Lock_t >(
          m_TransitPaths, info.txID,
          [info](const std::shared_ptr< TransitHop >& hop) -> bool {
            return info == hop->info;
          });
    }

    HopHandler_ptr
    PathContext::GetByUpstream(const RouterID& remote, const PathID_t& id)
    {
      auto own = MapGet< SyncOwnedPathsMap_t::Lock_t >(
          m_OurPaths, id,
          [](const PathSet_ptr) -> bool {
            // TODO: is this right?
            return true;
          },
          [remote, id](PathSet_ptr p) -> HopHandler_ptr {
            return p->GetByUpstream(remote, id);
          });
      if(own)
        return own;
      if(not m_AllowTransit)
        return nullptr;
      return MapGet< SyncTransitMap_t::Lock_t >(
          m_TransitPaths, id,
          [remote](const std::shared_ptr< TransitHop >& hop) -> bool {
            return hop->info.upstream == remote;
          },
          [](const std::shared_ptr< TransitHop >& h) -> HopHandler_ptr {
            return h;
          });
    }

    bool
    PathContext::TransitHopPreviousIsRouter(const PathID_t& path,
                                            const RouterID& otherRouter)
    {
      if(not m_AllowTransit)
        return false;
      SyncTransitMap_t::Lock_t lock(&m_TransitPaths.first);
      auto itr = m_TransitPaths.second.find(path);
      if(itr == m_TransitPaths.second.end())
        return false;
      return itr->second->info.downstream == otherRouter;
    }

    HopHandler_ptr
    PathContext::GetByDownstream(const RouterID& remote, const PathID_t& id)
    {
      if(not m_AllowTransit)
        return nullptr;
      return MapGet< SyncTransitMap_t::Lock_t >(
          m_TransitPaths, id,
          [remote](const std::shared_ptr< TransitHop >& hop) -> bool {
            return hop->info.downstream == remote;
          },
          [](const std::shared_ptr< TransitHop >& h) -> HopHandler_ptr {
            return h;
          });
    }

    PathSet_ptr
    PathContext::GetLocalPathSet(const PathID_t& id)
    {
      auto& map = m_OurPaths;
      SyncOwnedPathsMap_t::Lock_t lock(&map.first);
      auto itr = map.second.find(id);
      if(itr != map.second.end())
      {
        return itr->second;
      }
      return nullptr;
    }

    const byte_t*
    PathContext::OurRouterID() const
    {
      return m_Router->pubkey();
    }

    AbstractRouter*
    PathContext::Router()
    {
      return m_Router;
    }

    HopHandler_ptr
    PathContext::GetPathForTransfer(const PathID_t& id)
    {
      if(not m_AllowTransit)
        return nullptr;
      RouterID us(OurRouterID());
      auto& map = m_TransitPaths;
      {
        SyncTransitMap_t::Lock_t lock(&map.first);
        auto range = map.second.equal_range(id);
        for(auto i = range.first; i != range.second; ++i)
        {
          if(i->second->info.upstream == us)
            return i->second;
        }
      }
      return nullptr;
    }

    void
    PathContext::PumpUpstream()
    {
      if(m_AllowTransit)
      {
        m_TransitPaths.ForEach(
            [&](auto& ptr) { ptr->FlushUpstream(m_Router); });
      }
      // m_OurPaths.ForEach([&](auto& ptr) { ptr->UpstreamFlush(m_Router); });
    }

    void
    PathContext::PumpDownstream()
    {
      if(m_AllowTransit)
      {
        m_TransitPaths.ForEach(
            [&](auto& ptr) { ptr->FlushDownstream(m_Router); });
      }
      //  m_OurPaths.ForEach([&](auto& ptr) { ptr->DownstreamFlush(m_Router);
      //  });
    }

    void
    PathContext::PumpForSession(const RouterID pk, bool inbound)
    {
      if(m_AllowTransit)
      {
        m_TransitPaths.ForEach([&](auto& ptr) {
          if(ptr->info.upstream == pk)
          {
            if(inbound)
              ptr->FlushDownstream(m_Router);
            else
              ptr->FlushUpstream(m_Router);
          }
          if(ptr->info.downstream == pk)
          {
            if(inbound)
              ptr->FlushUpstream(m_Router);
            else
              ptr->FlushDownstream(m_Router);
          }
        });
      }
      /*
      m_OurPaths.ForEach([&](auto& ptr) {
        ptr->ForEachPath([&](auto& path) {
          if(path->Upstream() == pk)
          {
            if(inbound)
              ptr->DownstreamFlush(m_Router);
            else
              ptr->UpstreamFlush(m_Router);
          }
        });
      });
      */
    }

    void
    PathContext::PutTransitHop(std::shared_ptr< TransitHop > hop)
    {
      if(not m_AllowTransit)
      {
        LogError("not putting transit hop we are not allowing transit traffic");
        return;
      }
      MapPut< SyncTransitMap_t::Lock_t >(m_TransitPaths, hop->info.txID, hop);
      MapPut< SyncTransitMap_t::Lock_t >(m_TransitPaths, hop->info.rxID, hop);
    }

    void
    PathContext::ExpirePaths(llarp_time_t now)
    {
      if(m_AllowTransit)
      {
        SyncTransitMap_t::Lock_t lock(&m_TransitPaths.first);
        auto& map = m_TransitPaths.second;
        auto itr  = map.begin();
        while(itr != map.end())
        {
          if(itr->second->Expired(now))
          {
            itr = map.erase(itr);
          }
          else
            ++itr;
        }
      }
    }

    routing::MessageHandler_ptr
    PathContext::GetHandler(const PathID_t& id)
    {
      routing::MessageHandler_ptr h = nullptr;
      auto pathset                  = GetLocalPathSet(id);
      if(pathset)
      {
        h = pathset->GetPathByID(id);
      }
      if(h)
        return h;
      if(m_AllowTransit)
      {
        const RouterID us(OurRouterID());
        auto& map = m_TransitPaths;
        {
          SyncTransitMap_t::Lock_t lock(&map.first);
          auto range = map.second.equal_range(id);
          for(auto i = range.first; i != range.second; ++i)
          {
            if(i->second->info.upstream == us)
              return i->second;
          }
        }
      }
      return nullptr;
    }

    void
    PathContext::RemovePathSet(PathSet_ptr set)
    {
      SyncOwnedPathsMap_t::Lock_t lock(&m_OurPaths.first);
      auto& map = m_OurPaths.second;
      auto itr  = map.begin();
      while(itr != map.end())
      {
        if(itr->second.get() == set.get())
          itr = map.erase(itr);
        else
          ++itr;
      }
    }
  }  // namespace path
}  // namespace llarp
