#include "ev_libuv.hpp"
#include "net/net_addr.hpp"

namespace libuv
{
  /// tcp connection glue between llarp and libuv
  struct conn_glue
  {
    uv_tcp_t m_Handle{};
    uv_connect_t m_Connect{};
    uv_timer_t m_Ticker{};
    llarp_tcp_connecter* const m_TCP;
    llarp_tcp_acceptor* const m_Accept;
    llarp_tcp_conn m_Conn{};
    llarp::Addr m_Addr;

    std::deque< std::vector< char > > m_WriteQueue;

    conn_glue(uv_loop_t* loop, llarp_tcp_connecter* tcp, const sockaddr* addr)
        : m_TCP(tcp), m_Accept(nullptr), m_Addr(*addr)
    {
      m_Connect.data = this;
      m_Handle.data  = this;
      uv_tcp_init(loop, &m_Handle);
      m_Ticker.data = this;
      uv_timer_init(loop, &m_Ticker);
      m_Conn.close = &ExplicitClose;
      m_Conn.write = &ExplicitWrite;
    }

    conn_glue(uv_loop_t* loop, llarp_tcp_acceptor* tcp, const sockaddr* addr)
        : m_TCP(nullptr), m_Accept(tcp), m_Addr(*addr)
    {
      m_Handle.data = this;
      uv_tcp_init(loop, &m_Handle);
      m_Ticker.data = this;
      uv_timer_init(loop, &m_Ticker);
      m_Accept->close = &ExplicitCloseAccept;
      m_Conn.write    = nullptr;
      m_Conn.closed   = nullptr;
    }

    conn_glue(conn_glue* parent) : m_TCP(nullptr), m_Accept(nullptr)
    {
      m_Conn.close  = &ExplicitClose;
      m_Conn.write  = &ExplicitWrite;
      m_Handle.data = this;
      uv_tcp_init(parent->m_Handle.loop, &m_Handle);
      m_Ticker.data = this;
      uv_timer_init(parent->m_Handle.loop, &m_Ticker);
    }

    static void
    OnOutboundConnect(uv_connect_t* c, int status)
    {
      auto* self = static_cast< conn_glue* >(c->data);
      self->HandleConnectResult(status);
    }

    bool
    ConnectAsync()
    {
      return uv_tcp_connect(&m_Connect, &m_Handle, m_Addr, &OnOutboundConnect)
          != -1;
    }

    static void
    ExplicitClose(llarp_tcp_conn* conn)
    {
      static_cast< conn_glue* >(conn->impl)->Close();
    }
    static void
    ExplicitCloseAccept(llarp_tcp_acceptor* tcp)
    {
      static_cast< conn_glue* >(tcp->impl)->Close();
    }

    static ssize_t
    ExplicitWrite(llarp_tcp_conn* conn, const byte_t* ptr, size_t sz)
    {
      return static_cast< conn_glue* >(conn->impl)->WriteAsync((char*)ptr, sz);
    }

    static void
    OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
    {
      if(nread >= 0)
      {
        static_cast< conn_glue* >(stream->data)->Read(buf->base, nread);
      }
      else if(nread < 0)
      {
        static_cast< conn_glue* >(stream->data)->Close();
      }
      delete[] buf->base;
    }

    static void
    Alloc(uv_handle_t* /*unused*/, size_t suggested_size, uv_buf_t* buf)
    {
      buf->base = new char[suggested_size];
      buf->len  = suggested_size;
    }

    void
    Read(const char* ptr, ssize_t sz)
    {
      if(m_Conn.read != nullptr)
      {
        llarp::LogDebug("tcp read ", sz, " bytes");
        const llarp_buffer_t buf(ptr, sz);
        m_Conn.read(&m_Conn, buf);
      }
    }

    void
    HandleConnectResult(int status)
    {
      if((m_TCP != nullptr) && (m_TCP->connected != nullptr))
      {
        if(status == 0)
        {
          m_Conn.impl  = this;
          m_Conn.loop  = m_TCP->loop;
          m_Conn.close = &ExplicitClose;
          m_Conn.write = &ExplicitWrite;
          m_TCP->connected(m_TCP, &m_Conn);
          Start();
        }
        else if(m_TCP->error != nullptr)
        {
          llarp::LogError("failed to connect tcp ", uv_strerror(status));
          m_TCP->error(m_TCP);
        }
      }
    }

    void
    WriteFail()
    {
      if(m_Conn.close != nullptr)
      {
        m_Conn.close(&m_Conn);
      }
    }

    uv_stream_t*
    Stream()
    {
      return (uv_stream_t*)&m_Handle;
    }

    static void
    OnWritten(uv_write_t* req, int status)
    {
      if(status != 0)
      {
        llarp::LogError("write failed on tcp: ", uv_strerror(status));
        static_cast< conn_glue* >(req->data)->Close();
        return;
      }
      static_cast< conn_glue* >(req->data)->DrainOne();
    }

    void
    DrainOne()
    {
      m_WriteQueue.pop_front();
    }

    int
    WriteAsync(char* data, size_t sz)
    {
      m_WriteQueue.emplace_back(sz);
      std::copy_n(data, sz, m_WriteQueue.back().begin());
      auto buf  = uv_buf_init(m_WriteQueue.back().data(), sz);
      auto* req = new uv_write_t();
      req->data = this;
      return uv_write(req, Stream(), &buf, 1, &OnWritten) == 0 ? sz : 0;
    }

    static void
    OnClosed(uv_handle_t* h)
    {
      static_cast< conn_glue* >(h->data)->HandleClosed();
    }

    void
    HandleClosed()
    {
      m_Handle.data = nullptr;
      if((m_Accept != nullptr) && (m_Accept->closed != nullptr))
      {
        m_Accept->impl = nullptr;
        m_Accept->closed(m_Accept);
      }
      m_Conn.impl = nullptr;
      if(m_Conn.closed != nullptr)
      {
        m_Conn.closed(&m_Conn);
      }
      delete this;
    }

    void
    Close()
    {
      llarp::LogDebug("close tcp connection");
      uv_timer_stop(&m_Ticker);
      uv_close((uv_handle_t*)&m_Handle, &OnClosed);
    }

    static void
    OnAccept(uv_stream_t* stream, int status)
    {
      if(status == 0)
      {
        static_cast< conn_glue* >(stream->data)->Accept();
      }
      else
      {
        llarp::LogError("tcp accept failed: ", uv_strerror(status));
      }
    }

    static void
    OnTick(uv_timer_t* t)
    {
      static_cast< conn_glue* >(t->data)->Tick();
      uv_timer_again(t);
    }

    void
    Tick()
    {
      if((m_Accept != nullptr) && (m_Accept->tick != nullptr))
      {
        m_Accept->tick(m_Accept);
      }
      if(m_Conn.tick != nullptr)
      {
        m_Conn.tick(&m_Conn);
      }
    }

    void
    Start()
    {
      auto result = uv_timer_start((uv_timer_t*)&m_Ticker, &OnTick, 10, 10);
      if(result != 0)
      {
        llarp::LogError("failed to start timer ", uv_strerror(result));
      }
      result = uv_read_start(Stream(), &Alloc, &OnRead);
      if(result != 0)
      {
        llarp::LogError("failed to start reader ", uv_strerror(result));
      }
    }

    void
    Accept()
    {
      if((m_Accept != nullptr) && (m_Accept->accepted != nullptr))
      {
        auto* child = new conn_glue(this);
        llarp::LogDebug("accepted new connection");
        child->m_Conn.impl  = child;
        child->m_Conn.loop  = m_Accept->loop;
        child->m_Conn.close = &ExplicitClose;
        child->m_Conn.write = &ExplicitWrite;
        auto res            = uv_accept(Stream(), child->Stream());
        if(res != 0)
        {
          llarp::LogError("failed to accept tcp connection ", uv_strerror(res));
          child->Close();
          return;
        }
        m_Accept->accepted(m_Accept, &child->m_Conn);
        child->Start();
      }
    }

    bool
    Server()
    {
      m_Accept->close = &ExplicitCloseAccept;
      return uv_tcp_bind(&m_Handle, m_Addr, 0) == 0
          && uv_listen(Stream(), 5, &OnAccept) == 0;
    }
  };

  struct udp_glue
  {
    uv_udp_t m_Handle{};
    uv_check_t m_Ticker{};
    llarp_udp_io* const m_UDP;
    llarp::Addr m_Addr;
    bool gotpkts;

    udp_glue(uv_loop_t* loop, llarp_udp_io* udp, const sockaddr* src)
        : m_UDP(udp), m_Addr(*src)
    {
      m_Handle.data = this;
      m_Ticker.data = this;
      gotpkts       = false;
      uv_udp_init(loop, &m_Handle);
      uv_check_init(loop, &m_Ticker);
    }

    static void
    Alloc(uv_handle_t* /*unused*/, size_t suggested_size, uv_buf_t* buf)
    {
      buf->base = new char[suggested_size];
      buf->len  = suggested_size;
    }

    static void
    OnRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
           const struct sockaddr* addr, unsigned /*unused*/)
    {
      if(addr != nullptr)
      {
        static_cast< udp_glue* >(handle->data)->RecvFrom(nread, buf, addr);
      }
      delete[] buf->base;
    }

    void
    RecvFrom(ssize_t sz, const uv_buf_t* buf, const struct sockaddr* fromaddr)
    {
      if(sz >= 0 && (m_UDP != nullptr) && (m_UDP->recvfrom != nullptr))
      {
        const size_t pktsz = sz;
        const llarp_buffer_t pkt{(const byte_t*)buf->base, pktsz};
        m_UDP->recvfrom(m_UDP, fromaddr, ManagedBuffer{pkt});
        gotpkts = true;
      }
    }

    static void
    OnTick(uv_check_t* t)
    {
      static_cast< udp_glue* >(t->data)->Tick();
    }

    void
    Tick()
    {
      if((m_UDP != nullptr) && (m_UDP->tick != nullptr))
      {
        m_UDP->tick(m_UDP);
      }
    }

    static int
    SendTo(llarp_udp_io* udp, const sockaddr* to, const byte_t* ptr, size_t sz)
    {
      auto* self   = static_cast< udp_glue* >(udp->impl);
      uv_buf_t buf = uv_buf_init((char*)ptr, sz);
      return uv_udp_try_send(&self->m_Handle, &buf, 1, to);
    }

    bool
    Bind()
    {
      auto ret = uv_udp_bind(&m_Handle, m_Addr, 0);
      if(ret != 0)
      {
        llarp::LogError("failed to bind to ", m_Addr, " ", uv_strerror(ret));
        return false;
      }
      if(uv_udp_recv_start(&m_Handle, &Alloc, &OnRecv) != 0)
      {
        llarp::LogError("failed to start recving packets via ", m_Addr);
        return false;
      }
      if(uv_check_start(&m_Ticker, &OnTick) != 0)
      {
        llarp::LogError("failed to start ticker");
        return false;
      }
      if(uv_fileno((const uv_handle_t*)&m_Handle, &m_UDP->fd) != 0)
      {
        return false;
      }
      m_UDP->sendto = &SendTo;
      return true;
    }

    static void
    OnClosed(uv_handle_t* h)
    {
      auto* glue        = static_cast< udp_glue* >(h->data);
      glue->m_UDP->impl = nullptr;
      delete glue;
    }

    void
    Close()
    {
      uv_check_stop(&m_Ticker);
      uv_close((uv_handle_t*)&m_Handle, &OnClosed);
    }
  };

  struct tun_glue
  {
    uv_poll_t m_Handle{};
    uv_check_t m_Ticker{};
    llarp_tun_io* const m_Tun;
    device* const m_Device;
    byte_t m_Buffer[1500]{};
    bool readpkt;

    tun_glue(llarp_tun_io* tun) : m_Tun(tun), m_Device(tuntap_init())
    {
      m_Handle.data = this;
      m_Ticker.data = this;
      readpkt       = false;
    }

    ~tun_glue()
    {
      tuntap_destroy(m_Device);
    }

    static void
    OnTick(uv_check_t* timer)
    {
      static_cast< tun_glue* >(timer->data)->Tick();
    }

    static void
    OnPoll(uv_poll_t* h, int /*unused*/, int events)
    {
      if((events & UV_READABLE) != 0)
      {
        static_cast< tun_glue* >(h->data)->Read();
      }
    }

    void
    Read()
    {
      auto sz = tuntap_read(m_Device, m_Buffer, sizeof(m_Buffer));
      if(sz > 0)
      {
        llarp::LogDebug("tun read ", sz);
        llarp_buffer_t pkt(m_Buffer, sz);
        if((m_Tun != nullptr) && (m_Tun->recvpkt != nullptr))
        {
          m_Tun->recvpkt(m_Tun, pkt);
        }
      }
    }

    void
    Tick()
    {
      if(m_Tun->before_write != nullptr)
      {
        m_Tun->before_write(m_Tun);
      }
      if(m_Tun->tick != nullptr)
      {
        m_Tun->tick(m_Tun);
      }
    }

    static void
    OnClosed(uv_handle_t* h)
    {
      auto* self = static_cast< tun_glue* >(h->data);
      delete self;
    }

    void
    Close()
    {
      uv_close((uv_handle_t*)&m_Handle, &OnClosed);
    }

    bool
    Write(const byte_t* pkt, size_t sz)
    {
      return tuntap_write(m_Device, (void*)pkt, sz) != -1;
    }

    static bool
    WritePkt(llarp_tun_io* tun, const byte_t* pkt, size_t sz)
    {
      return static_cast< tun_glue* >(tun->impl)->Write(pkt, sz);
    }

    bool
    Init(uv_loop_t* loop)
    {
      strncpy(m_Device->if_name, m_Tun->ifname, sizeof(m_Device->if_name));
      if(tuntap_start(m_Device, TUNTAP_MODE_TUNNEL, 0) == -1)
      {
        llarp::LogError("failed to start up ", m_Tun->ifname);
        return false;
      }
      if(tuntap_set_ip(m_Device, m_Tun->ifaddr, m_Tun->ifaddr, m_Tun->netmask)
         == -1)
      {
        llarp::LogError("failed to set address on ", m_Tun->ifname);
        return false;
      }
      if(tuntap_up(m_Device) == -1)
      {
        llarp::LogError("failed to put up ", m_Tun->ifname);
        return false;
      }
      if(m_Device->tun_fd == -1)
      {
        llarp::LogError("tun interface ", m_Tun->ifname,
                        " has invalid fd: ", m_Device->tun_fd);
        return false;
      }
      if(uv_poll_init(loop, &m_Handle, m_Device->tun_fd) == -1)
      {
        llarp::LogError("failed to start polling on ", m_Tun->ifname);
        return false;
      }
      if(uv_poll_start(&m_Handle, UV_READABLE, &OnPoll) != 0)
      {
        llarp::LogError("failed to start polling on ", m_Tun->ifname);
        return false;
      }
      if(uv_check_init(loop, &m_Ticker) != 0
         || uv_check_start(&m_Ticker, &OnTick) != 0)
      {
        llarp::LogError("failed to set up tun interface timer for ",
                        m_Tun->ifname);
        return false;
      }
      m_Tun->writepkt = &WritePkt;
      return true;
    }
  };

  bool
  Loop::init()
  {
    m_Impl.reset(uv_loop_new());
    if(uv_loop_init(m_Impl.get()) == -1)
    {
      return false;
    }
    uv_loop_configure(m_Impl.get(), UV_LOOP_BLOCK_SIGNAL, SIGPIPE);
    m_TickTimer.data = this;
    m_Run.store(true);
    return uv_timer_init(m_Impl.get(), &m_TickTimer) != -1;
  }

  void
  Loop::update_time()
  {
    uv_update_time(m_Impl.get());
  }

  bool
  Loop::running() const
  {
    return m_Run.load();
  }

  llarp_time_t
  Loop::time_now() const
  {
    return llarp::time_now_ms();
  }

  bool
  Loop::tcp_connect(llarp_tcp_connecter* tcp, const sockaddr* addr)
  {
    auto* impl = new conn_glue(m_Impl.get(), tcp, addr);
    tcp->impl  = impl;
    if(impl->ConnectAsync())
    {
      return true;
    }
    delete impl;
    tcp->impl = nullptr;
    return false;
  }

  static void
  OnTickTimeout(uv_timer_t* timer)
  {
    uv_stop(timer->loop);
  }

  int
  Loop::tick(int ms)
  {
    uv_timer_start(&m_TickTimer, &OnTickTimeout, ms, 0);
    uv_run(m_Impl.get(), UV_RUN_ONCE);
    return 0;
  }

  void
  Loop::stop()
  {
    llarp::LogInfo("stopping event loop");
    m_Run.store(false);
  }

  bool
  Loop::udp_listen(llarp_udp_io* udp, const sockaddr* src)
  {
    auto* impl = new udp_glue(m_Impl.get(), udp, src);
    udp->impl  = impl;
    if(impl->Bind())
    {
      m_CloseFuncs.emplace_back(std::bind(&udp_glue::Close, impl));
      return true;
    }
    return false;
  }

  bool
  Loop::udp_close(llarp_udp_io* udp)
  {
    if(udp == nullptr)
    {
      return false;
    }
    auto* glue = static_cast< udp_glue* >(udp->impl);
    if(glue == nullptr)
    {
      return false;
    }
    glue->Close();
    return true;
  }

  bool
  Loop::tun_listen(llarp_tun_io* tun)
  {
    auto* glue = new tun_glue(tun);
    tun->impl  = glue;
    if(glue->Init(m_Impl.get()))
    {
      m_CloseFuncs.emplace_back(std::bind(&tun_glue ::Close, glue));
      return true;
    }
    delete glue;
    return false;
  }

  bool
  Loop::tcp_listen(llarp_tcp_acceptor* tcp, const sockaddr* addr)
  {
    auto* glue = new conn_glue(m_Impl.get(), tcp, addr);
    tcp->impl  = glue;
    if(glue->Server())
    {
      return true;
    }
    tcp->impl = nullptr;
    delete glue;
    return false;
  }

}  // namespace libuv
