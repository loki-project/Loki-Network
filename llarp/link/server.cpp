#include <link/server.hpp>

#include <crypto/crypto.hpp>
#include <util/fs.hpp>

namespace llarp
{
  static constexpr size_t MaxSessionsPerKey = 16;

  ILinkLayer::ILinkLayer(const SecretKey& routerEncSecret, GetRCFunc getrc,
                         LinkMessageHandler handler, SignBufferFunc signbuf,
                         SessionEstablishedHandler establishedSession,
                         SessionRenegotiateHandler reneg,
                         TimeoutHandler timeout, SessionClosedHandler closed)
      : HandleMessage(handler)
      , HandleTimeout(timeout)
      , Sign(signbuf)
      , GetOurRC(getrc)
      , SessionEstablished(establishedSession)
      , SessionClosed(closed)
      , SessionRenegotiate(reneg)
      , m_RouterEncSecret(routerEncSecret)
  {
  }

  ILinkLayer::~ILinkLayer()
  {
  }

  bool
  ILinkLayer::HasSessionTo(const RouterID& id)
  {
    Lock l(&m_AuthedLinksMutex);
    return m_AuthedLinks.find(id) != m_AuthedLinks.end();
  }

  void
  ILinkLayer::ForEachSession(std::function< void(const ILinkSession*) > visit,
                             bool randomize) const
  {
    Lock l(&m_AuthedLinksMutex);
    if(m_AuthedLinks.size() == 0)
      return;
    const size_t sz = randint() % m_AuthedLinks.size();
    auto itr        = m_AuthedLinks.begin();
    auto begin      = itr;
    if(randomize)
    {
      std::advance(itr, sz);
      begin = itr;
    }
    while(itr != m_AuthedLinks.end())
    {
      visit(itr->second.get());
      ++itr;
    }
    if(randomize)
    {
      itr = m_AuthedLinks.begin();
      while(itr != begin)
      {
        visit(itr->second.get());
        ++itr;
      }
    }
  }

  bool
  ILinkLayer::VisitSessionByPubkey(const RouterID& pk,
                                   std::function< bool(ILinkSession*) > visit)
  {
    Lock l(&m_AuthedLinksMutex);
    auto itr = m_AuthedLinks.find(pk);
    if(itr != m_AuthedLinks.end())
    {
      return visit(itr->second.get());
    }
    return false;
  }

  void
  ILinkLayer::ForEachSession(std::function< void(ILinkSession*) > visit)
  {
    Lock l(&m_AuthedLinksMutex);
    auto itr = m_AuthedLinks.begin();
    while(itr != m_AuthedLinks.end())
    {
      visit(itr->second.get());
      ++itr;
    }
  }

  bool
  ILinkLayer::Configure(llarp_ev_loop_ptr loop, const std::string& ifname,
                        int af, uint16_t port)
  {
    m_Loop         = std::move(loop);
    m_udp.user     = this;
    m_udp.recvfrom = &ILinkLayer::udp_recv_from;
    m_udp.tick     = &ILinkLayer::udp_tick;
    if(ifname == "*")
    {
      if(!AllInterfaces(af, m_ourAddr))
        return false;
    }
    else if(!GetIFAddr(ifname, m_ourAddr, af))
      return false;
    m_ourAddr.port(port);
    return llarp_ev_add_udp(m_Loop.get(), &m_udp, m_ourAddr) != -1;
  }

  void
  ILinkLayer::Pump()
  {
    auto _now = Now();
    {
      Lock lock(&m_AuthedLinksMutex);
      auto itr = m_AuthedLinks.begin();
      while(itr != m_AuthedLinks.end())
      {
        if(itr->second.get() && !itr->second->TimedOut(_now))
        {
          itr->second->Pump();
          ++itr;
        }
        else
        {
          llarp::LogInfo("session to ", RouterID(itr->second->GetPubKey()),
                         " timed out");
          itr = m_AuthedLinks.erase(itr);
        }
      }
    }
    {
      Lock lock(&m_PendingMutex);

      auto itr = m_Pending.begin();
      while(itr != m_Pending.end())
      {
        if(itr->second.get() && !itr->second->TimedOut(_now))
        {
          itr->second->Pump();
          ++itr;
        }
        else
        {
          LogInfo("pending session at ", itr->first, " timed out");
          itr = m_Pending.erase(itr);
        }
      }
    }
  }

  bool
  ILinkLayer::MapAddr(const RouterID& pk, ILinkSession* s)
  {
    Lock l_authed(&m_AuthedLinksMutex);
    Lock l_pending(&m_PendingMutex);
    llarp::Addr addr = s->GetRemoteEndpoint();
    auto itr         = m_Pending.find(addr);
    if(itr != m_Pending.end())
    {
      if(m_AuthedLinks.count(pk) > MaxSessionsPerKey)
      {
        s->Close();
        return false;
      }
      m_AuthedLinks.emplace(pk, itr->second);
      itr = m_Pending.erase(itr);
      return true;
    }
    return false;
  }

  bool
  ILinkLayer::PickAddress(const RouterContact& rc,
                          llarp::AddressInfo& picked) const
  {
    std::string OurDialect = Name();
    for(const auto& addr : rc.addrs)
    {
      if(addr.dialect == OurDialect)
      {
        picked = addr;
        return true;
      }
    }
    return false;
  }

  util::StatusObject
  ILinkLayer::ExtractStatus() const
  {
    std::vector< util::StatusObject > pending, established;

    {
      Lock l(&m_PendingMutex);
      std::transform(m_Pending.cbegin(), m_Pending.cend(),
                     std::back_inserter(pending),
                     [](const auto& item) -> util::StatusObject {
                       return item.second->ExtractStatus();
                     });
    }
    {
      Lock l(&m_AuthedLinksMutex);
      std::transform(m_AuthedLinks.cbegin(), m_AuthedLinks.cend(),
                     std::back_inserter(established),
                     [](const auto& item) -> util::StatusObject {
                       return item.second->ExtractStatus();
                     });
    }

    return {{"name", Name()},
            {"rank", uint64_t(Rank())},
            {"addr", m_ourAddr.ToString()},
            {"sessions",
             util::StatusObject{{"pending", pending},
                                {"established", established}}}};
  }

  bool
  ILinkLayer::TryEstablishTo(RouterContact rc)
  {
    {
      Lock l(&m_AuthedLinksMutex);
      if(m_AuthedLinks.count(rc.pubkey) >= MaxSessionsPerKey)
        return false;
    }
    llarp::AddressInfo to;
    if(!PickAddress(rc, to))
      return false;
    const llarp::Addr addr(to);
    {
      Lock l(&m_PendingMutex);
      if(m_Pending.count(addr) >= MaxSessionsPerKey)
        return false;
    }
    std::shared_ptr< ILinkSession > s = NewOutboundSession(rc, to);
    if(PutSession(s))
    {
      s->Start();
      return true;
    }
    return false;
  }

  bool
  ILinkLayer::Start(Logic* l)
  {
    m_Logic = l;
    ScheduleTick(100);
    return true;
  }

  void
  ILinkLayer::Tick(llarp_time_t now)
  {
    {
      Lock l(&m_AuthedLinksMutex);
      auto itr = m_AuthedLinks.begin();
      while(itr != m_AuthedLinks.end())
      {
        itr->second->Tick(now);
        ++itr;
      }
    }

    {
      Lock l(&m_PendingMutex);
      auto itr = m_Pending.begin();
      while(itr != m_Pending.end())
      {
        itr->second->Tick(now);
        ++itr;
      }
    }
  }

  void
  ILinkLayer::Stop()
  {
    if(m_Logic && tick_id)
      m_Logic->remove_call(tick_id);
    {
      Lock l(&m_AuthedLinksMutex);
      auto itr = m_AuthedLinks.begin();
      while(itr != m_AuthedLinks.end())
      {
        itr->second->Close();
        ++itr;
      }
    }
    {
      Lock l(&m_PendingMutex);
      auto itr = m_Pending.begin();
      while(itr != m_Pending.end())
      {
        itr->second->Close();
        ++itr;
      }
    }
  }

  void
  ILinkLayer::CloseSessionTo(const RouterID& remote)
  {
    Lock l(&m_AuthedLinksMutex);
    RouterID r = remote;
    llarp::LogInfo("Closing all to ", r);
    auto range = m_AuthedLinks.equal_range(r);
    auto itr   = range.first;
    while(itr != range.second)
    {
      itr->second->Close();
      itr = m_AuthedLinks.erase(itr);
    }
  }

  void
  ILinkLayer::KeepAliveSessionTo(const RouterID& remote)
  {
    Lock l(&m_AuthedLinksMutex);
    auto range = m_AuthedLinks.equal_range(remote);
    auto itr   = range.first;
    while(itr != range.second)
    {
      if(itr->second->ShouldPing())
        itr->second->SendKeepAlive();
      ++itr;
    }
  }

  bool
  ILinkLayer::SendTo(const RouterID& remote, const llarp_buffer_t& buf)
  {
    ILinkSession* s = nullptr;
    {
      Lock l(&m_AuthedLinksMutex);
      auto range = m_AuthedLinks.equal_range(remote);
      auto itr   = range.first;
      // pick lowest backlog session
      size_t min = std::numeric_limits< size_t >::max();

      while(itr != range.second)
      {
        auto backlog = itr->second->SendQueueBacklog();
        if(backlog < min)
        {
          s   = itr->second.get();
          min = backlog;
        }
        ++itr;
      }
    }
    return s && s->SendMessageBuffer(buf);
  }

  bool
  ILinkLayer::GetOurAddressInfo(llarp::AddressInfo& addr) const
  {
    addr.dialect = Name();
    addr.pubkey  = TransportPubKey();
    addr.rank    = Rank();
    addr.port    = m_ourAddr.port();
    addr.ip      = *m_ourAddr.addr6();
    return true;
  }

  const byte_t*
  ILinkLayer::TransportPubKey() const
  {
    return llarp::seckey_topublic(TransportSecretKey());
  }

  const SecretKey&
  ILinkLayer::TransportSecretKey() const
  {
    return m_SecretKey;
  }

  bool
  ILinkLayer::GenEphemeralKeys()
  {
    return KeyGen(m_SecretKey);
  }

  bool
  ILinkLayer::EnsureKeys(const char* f)
  {
    fs::path fpath(f);
    llarp::SecretKey keys;
    std::error_code ec;
    if(!fs::exists(fpath, ec))
    {
      if(!KeyGen(m_SecretKey))
        return false;
      // generated new keys
      if(!BEncodeWriteFile< decltype(keys), 128 >(f, m_SecretKey))
        return false;
    }
    // load keys
    if(!BDecodeReadFile(f, m_SecretKey))
    {
      llarp::LogError("Failed to load keyfile ", f);
      return false;
    }
    return true;
  }

  bool
  ILinkLayer::PutSession(const std::shared_ptr< ILinkSession >& s)
  {
    static constexpr size_t MaxSessionsPerEndpoint = 5;
    Lock lock(&m_PendingMutex);
    llarp::Addr addr = s->GetRemoteEndpoint();
    if(m_Pending.count(addr) >= MaxSessionsPerEndpoint)
      return false;
    m_Pending.emplace(addr, s);
    return true;
  }

  void
  ILinkLayer::OnTick(uint64_t interval)
  {
    auto now = Now();
    Tick(now);
    ScheduleTick(interval);
  }

  void
  ILinkLayer::ScheduleTick(uint64_t interval)
  {
    tick_id = m_Logic->call_later({interval, this, &ILinkLayer::on_timer_tick});
  }

}  // namespace llarp
