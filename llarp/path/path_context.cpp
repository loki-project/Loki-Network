#include <path/path_context.hpp>

#include <messages/relay_commit.hpp>
#include <path/path.hpp>
#include <router/abstractrouter.hpp>

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

    llarp_threadpool*
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
                             const std::array< EncryptedFrame, 8 >& frames)
    {
      auto msg = std::make_shared< const LR_CommitMessage >(frames);

      LogDebug("forwarding LRCM to ", nextHop);
      if(m_Router->HasSessionTo(nextHop))
      {
        return m_Router->SendToOrQueue(nextHop, msg.get());
      }
      const RouterID router   = nextHop;
      AbstractRouter* const r = m_Router;
      m_Router->EnsureRouter(
          nextHop, [msg, r, router](const std::vector< RouterContact >& found) {
            if(found.size())
            {
              r->TryConnectAsync(found[0], 1);
              r->SendToOrQueue(router, msg.get());
            }
            else
              LogError("dropped LRCM to ", router,
                       " as we cannot find in via DHT");
          });
      LogInfo("we are not directly connected to ", router,
              " so we need to do a lookup");
      return true;
    }
    template < typename Map_t, typename Key_t, typename CheckValue_t,
               typename GetFunc_t >
    HopHandler_ptr
    MapGet(Map_t& map, const Key_t& k, CheckValue_t check, GetFunc_t get)
    {
      util::Lock lock(&map.first);
      auto range = map.second.equal_range(k);
      for(auto i = range.first; i != range.second; ++i)
      {
        if(check(i->second))
          return get(i->second);
      }
      return nullptr;
    }

    template < typename Map_t, typename Key_t, typename CheckValue_t >
    bool
    MapHas(Map_t& map, const Key_t& k, CheckValue_t check)
    {
      util::Lock lock(&map.first);
      auto range = map.second.equal_range(k);
      for(auto i = range.first; i != range.second; ++i)
      {
        if(check(i->second))
          return true;
      }
      return false;
    }

    template < typename Map_t, typename Key_t, typename Value_t >
    void
    MapPut(Map_t& map, const Key_t& k, const Value_t& v)
    {
      util::Lock lock(&map.first);
      map.second.emplace(k, v);
    }

    template < typename Map_t, typename Visit_t >
    void
    MapIter(Map_t& map, Visit_t v)
    {
      util::Lock lock(map.first);
      for(const auto& item : map.second)
        v(item);
    }

    template < typename Map_t, typename Key_t, typename Check_t >
    void
    MapDel(Map_t& map, const Key_t& k, Check_t check)
    {
      util::Lock lock(map.first);
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
      MapPut(m_OurPaths, path->TXID(), set);
      MapPut(m_OurPaths, path->RXID(), set);
    }

    bool
    PathContext::HasTransitHop(const TransitHopInfo& info)
    {
      return MapHas(m_TransitPaths, info.txID,
                    [info](const std::shared_ptr< TransitHop >& hop) -> bool {
                      return info == hop->info;
                    });
    }

    HopHandler_ptr
    PathContext::GetByUpstream(const RouterID& remote, const PathID_t& id)
    {
      auto own = MapGet(
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

      return MapGet(
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
      util::Lock lock(&m_TransitPaths.first);
      auto itr = m_TransitPaths.second.find(path);
      if(itr == m_TransitPaths.second.end())
        return false;
      return itr->second->info.downstream == otherRouter;
    }

    HopHandler_ptr
    PathContext::GetByDownstream(const RouterID& remote, const PathID_t& id)
    {
      return MapGet(
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
      util::Lock lock(&map.first);
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
      RouterID us(OurRouterID());
      auto& map = m_TransitPaths;
      {
        util::Lock lock(&map.first);
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
    PathContext::PutTransitHop(std::shared_ptr< TransitHop > hop)
    {
      MapPut(m_TransitPaths, hop->info.txID, hop);
      MapPut(m_TransitPaths, hop->info.rxID, hop);
    }

    void
    PathContext::ExpirePaths(llarp_time_t now)
    {
      {
        util::Lock lock(&m_TransitPaths.first);
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
      {
        util::Lock lock(&m_OurPaths.first);
        auto& map = m_OurPaths.second;
        for(auto& item : map)
        {
          item.second->ExpirePaths(now);
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
      const RouterID us(OurRouterID());
      auto& map = m_TransitPaths;
      {
        util::Lock lock(&map.first);
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
    PathContext::RemovePathSet(PathSet_ptr set)
    {
      util::Lock lock(&m_OurPaths.first);
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
