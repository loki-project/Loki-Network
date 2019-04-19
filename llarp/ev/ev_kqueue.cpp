#include <ev/ev_kqueue.hpp>

namespace llarp
{
  inline int
  tcp_conn::read(byte_t* buf, size_t sz)
  {
    if(sz == 0)
    {
      if(tcp.read)
        tcp.read(&tcp, {nullptr, nullptr, 0});
      return 0;
    }
    if(_shouldClose)
      return -1;

    ssize_t amount = ::read(fd, buf, sz);

    if(amount >= 0)
    {
      if(tcp.read)
        tcp.read(&tcp, llarp_buffer_t(buf, amount));
    }
    else
    {
      if(errno == EAGAIN || errno == EWOULDBLOCK)
        return amount;
      _shouldClose = true;
      return -1;
    }
    return amount;
  }

  inline void
  tcp_conn::flush_write()
  {
    connected();
    ev_io::flush_write();
  }

  inline ssize_t
  tcp_conn::do_write(void* buf, size_t sz)
  {
    if(_shouldClose)
      return -1;

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
    // macintosh uses a weird sockopt
    return ::send(fd, buf, sz, MSG_NOSIGNAL);  // ignore sigpipe
#else
    return ::send(fd, buf, sz, 0);
#endif
  }

  inline void
  tcp_conn::connect()
  {
    socklen_t slen = sizeof(sockaddr_in);
    if(_addr.ss_family == AF_UNIX)
      slen = sizeof(sockaddr_un);
    else if(_addr.ss_family == AF_INET6)
      slen = sizeof(sockaddr_in6);
    int result = ::connect(fd, (const sockaddr*)&_addr, slen);
    if(result == 0)
    {
      llarp::LogDebug("Connected");
      connected();
    }
    else if(errno == EINPROGRESS)
    {
      llarp::LogDebug("connect in progress");
      errno = 0;
      return;
    }
    else if(_conn)
    {
      _conn->error(_conn);
    }
  }

  inline int
  tcp_serv::read(byte_t*, size_t)
  {
    int new_fd = ::accept(fd, nullptr, nullptr);
    if(new_fd == -1)
    {
      llarp::LogError("failed to accept on ", fd, ": ", strerror(errno));
      return -1;
    }
    // get flags
    int flags = fcntl(new_fd, F_GETFL, 0);
    if(flags == -1)
    {
      ::close(new_fd);
      return -1;
    }
    // set flags
    if(fcntl(new_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      llarp::LogError("Failed to set non block on ", fd, ": ", strerror(errno));
      ::close(new_fd);
      return -1;
    }
    // build handler
    llarp::tcp_conn* connimpl = new llarp::tcp_conn(loop, new_fd);
    if(loop->add_ev(connimpl, true))
    {
      // call callback
      if(tcp->accepted)
        tcp->accepted(tcp, &connimpl->tcp);
      return 0;
    }
    // cleanup error
    delete connimpl;
    return -1;
  }

  bool
  udp_listener::tick()
  {
    if(udp->tick)
      udp->tick(udp);
    return true;
  }

  int
  udp_listener::read(byte_t* buf, size_t sz)
  {
    sockaddr_in6 src;
    socklen_t slen = sizeof(sockaddr_in6);
    sockaddr* addr = (sockaddr*)&src;
    ssize_t ret    = ::recvfrom(fd, buf, sz, 0, addr, &slen);
    if(ret < 0)
    {
      llarp::LogWarn("recvfrom failed");
      return -1;
    }
    if(static_cast< size_t >(ret) > sz)
    {
      llarp::LogWarn("ret > sz");
      return -1;
    }
    if(!addr)
    {
      llarp::LogWarn("no source addr");
    }
    // Addr is the source
    udp->recvfrom(udp, addr, ManagedBuffer{llarp_buffer_t(buf, ret)});
    return 0;
  }

  int
  udp_listener::sendto(const sockaddr* to, const void* data, size_t sz)
  {
    socklen_t slen;
    switch(to->sa_family)
    {
      case AF_INET:
        slen = sizeof(struct sockaddr_in);
        break;
      case AF_INET6:
        slen = sizeof(struct sockaddr_in6);
        break;
      default:
        return -1;
    }
    if(!fd)
    {
      printf("kqueue sendto fd empty\n");
      return -1;
    }
    ssize_t sent = ::sendto(fd, data, sz, 0, to, slen);
    if(sent == -1 || errno)
    {
      llarp::LogError("failed to send udp: ", strerror(errno));
      errno = 0;
    }
    return sent;
  }

  int
  tun::sendto(__attribute__((unused)) const sockaddr* to,
              __attribute__((unused)) const void* data,
              __attribute__((unused)) size_t sz)
  {
    return -1;
  }

#ifdef __APPLE__
  ssize_t
  tun::do_write(void* buf, size_t sz)
  {
    iovec vecs[2];
    // TODO: IPV6
    uint32_t t       = htonl(AF_INET);
    vecs[0].iov_base = &t;
    vecs[0].iov_len  = sizeof(t);
    vecs[1].iov_base = buf;
    vecs[1].iov_len  = sz;
    return writev(fd, vecs, 2);
  }
#endif

  void
  tun::before_flush_write()
  {
    if(t->before_write)
    {
      t->before_write(t);
    }
  }

  bool
  tun::tick()
  {
    if(t->tick)
      t->tick(t);
    flush_write();
    return true;
  }

  int
  tun::read(byte_t* buf, size_t)
  {
// all BSDs have packet info except freebsd
#ifdef __FreeBSD__
    const ssize_t offset = 0;
#else
    const ssize_t offset = 4;
#endif

    ssize_t ret = ::read(fd, buf, 1500);
    if(ret > offset && t->recvpkt)
    {
      buf += offset;
      ret -= offset;
      llarp_buffer_t pkt(buf, ret);
      t->recvpkt(t, pkt);
    }
    return ret;
  }

  bool
  tun::setup()
  {
    llarp::LogDebug("set up tunif");
    if(tuntap_start(tunif, TUNTAP_MODE_TUNNEL, TUNTAP_ID_ANY) == -1)
      return false;

    if(tuntap_up(tunif) == -1)
      return false;
    if(tuntap_set_ifname(tunif, t->ifname) == -1)
      return false;
    llarp::LogInfo("set ", tunif->if_name, " to use address ", t->ifaddr);

    if(tuntap_set_ip(tunif, t->ifaddr, t->ifaddr, t->netmask) == -1)
      return false;
    fd = tunif->tun_fd;
    return fd != -1;
  }

};  // namespace llarp

llarp::ev_io*
llarp_kqueue_loop::bind_tcp(llarp_tcp_acceptor* tcp, const sockaddr* bindaddr)
{
  int fd = ::socket(bindaddr->sa_family, SOCK_STREAM, 0);
  if(fd == -1)
    return nullptr;
  socklen_t sz = sizeof(sockaddr_in);
  if(bindaddr->sa_family == AF_INET6)
  {
    sz = sizeof(sockaddr_in6);
  }
  else if(bindaddr->sa_family == AF_UNIX)
  {
    sz = sizeof(sockaddr_un);
  }
  if(::bind(fd, bindaddr, sz) == -1)
  {
    ::close(fd);
    return nullptr;
  }
  if(::listen(fd, 5) == -1)
  {
    ::close(fd);
    return nullptr;
  }
  // set non blocking
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags == -1)
  {
    ::close(fd);
    return nullptr;
  }
  if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    ::close(fd);
    return nullptr;
  }
  llarp::ev_io* serv = new llarp::tcp_serv(this, fd, tcp);
  tcp->impl          = serv;
  return serv;
}

llarp::ev_io*
llarp_kqueue_loop::create_tun(llarp_tun_io* tun)
{
  llarp::tun* t = new llarp::tun(tun, shared_from_this());
  if(t->setup())
    return t;
  delete t;
  return nullptr;
}

bool
llarp_kqueue_loop::init()
{
  if(kqueuefd == -1)
  {
    kqueuefd = kqueue();
  }
  return kqueuefd != -1;
}

bool
llarp_kqueue_loop::running() const
{
  return kqueuefd != -1;
}

bool
llarp_kqueue_loop::tcp_connect(llarp_tcp_connecter* tcp, const sockaddr* addr)
{
  int fd = ::socket(addr->sa_family, SOCK_STREAM, 0);
  if(fd == -1)
    return false;
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags == -1)
  {
    ::close(fd);
    return false;
  }
  if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    ::close(fd);
    return false;
  }

  llarp::tcp_conn* conn = new llarp::tcp_conn(this, fd, addr, tcp);
  add_ev(conn, true);
  conn->connect();
  return true;
}

int
llarp_kqueue_loop::tick(int ms)
{
  std::array< struct kevent, 1024 > events;
  int result;
  timespec t;
  t.tv_sec  = 0;
  t.tv_nsec = ms * 1000000UL;
  result    = kevent(kqueuefd, nullptr, 0, events.data(), events.size(), &t);
  // result: 0 is a timeout
  if(result > 0)
  {
    int idx = 0;
    while(idx < result)
    {
      llarp::ev_io* ev = static_cast< llarp::ev_io* >(events[idx].udata);
      if(ev)
      {
        if(events[idx].filter & EVFILT_READ)
        {
          IO([&]() -> ssize_t {
            return ev->read(
                readbuf, std::min(sizeof(readbuf), size_t(events[idx].data)));
          });
        }
        if(events[idx].filter & EVFILT_WRITE)
        {
          IO([&]() -> ssize_t {
            ev->flush_write_buffers(events[idx].data);
            return 0;
          });
        }
      }
      ++idx;
    }
  }
  if(result != -1)
    tick_listeners();
  return result;
}

int
llarp_kqueue_loop::run()
{
  timespec t;
  t.tv_sec  = 0;
  t.tv_nsec = 1000000UL * EV_TICK_INTERVAL;
  struct kevent events[1024];
  int result;
  do
  {
    result = kevent(kqueuefd, nullptr, 0, events, 1024, &t);
    // result: 0 is a timeout
    if(result > 0)
    {
      int idx = 0;
      while(idx < result)
      {
        llarp::ev_io* ev = static_cast< llarp::ev_io* >(events[idx].udata);
        if(ev)
        {
          if(events[idx].filter & EVFILT_READ)
            ev->read(readbuf,
                     std::min(sizeof(readbuf), size_t(events[idx].data)));
          if(events[idx].filter & EVFILT_WRITE)
            ev->flush_write_buffers(events[idx].data);
        }
        else
        {
          llarp::LogWarn("kqueue event ", idx, " udata wasnt an ev_io");
        }
        ++idx;
      }
    }
    if(result != -1)
      tick_listeners();
  } while(result != -1);
  return result;
}

int
llarp_kqueue_loop::udp_bind(const sockaddr* addr)
{
  socklen_t slen;
  llarp::LogDebug("kqueue bind affam", addr->sa_family);
  switch(addr->sa_family)
  {
    case AF_INET:
      slen = sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      slen = sizeof(struct sockaddr_in6);
      break;
#ifdef AF_LINK
#endif
#ifdef AF_PACKET
    case AF_PACKET:
      slen = sizeof(struct sockaddr_ll);
      break;
#endif
    default:
      llarp::LogError("unsupported address family");
      return -1;
  }
  int fd = socket(addr->sa_family, SOCK_DGRAM, 0);
  if(fd == -1)
  {
    perror("socket()");
    return -1;
  }

  if(addr->sa_family == AF_INET6)
  {
    // enable dual stack explicitly
    int dual = 1;
    if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual, sizeof(dual)) == -1)
    {
      // failed
      perror("setsockopt()");
      close(fd);
      return -1;
    }
  }
  llarp::Addr a(*addr);
  llarp::LogDebug("bind to ", a);
  // FreeBSD handbook said to do this
  if(addr->sa_family == AF_INET && INADDR_ANY)
    a._addr4.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(fd, addr, slen) == -1)
  {
    perror("bind()");
    close(fd);
    return -1;
  }
  return fd;
}

bool
llarp_kqueue_loop::udp_listen(llarp_udp_io* l, const sockaddr* src)
{
  auto ev = create_udp(l, src);
  if(ev)
    l->fd = ev->fd;
  return ev && add_ev(ev, false);
}

bool
llarp_kqueue_loop::close_ev(llarp::ev_io* ev)
{
  EV_SET(&ev->change, ev->fd, ev->flags, EV_DELETE, 0, 0, nullptr);
  return kevent(kqueuefd, &ev->change, 1, nullptr, 0, nullptr) != -1;
}

llarp::ev_io*
llarp_kqueue_loop::create_udp(llarp_udp_io* l, const sockaddr* src)
{
  int fd = udp_bind(src);
  if(fd == -1)
    return nullptr;
  llarp::udp_listener* listener = new llarp::udp_listener(fd, l);
  l->impl                       = listener;
  return listener;
}

bool
llarp_kqueue_loop::add_ev(llarp::ev_io* ev, bool w)
{
  ev->flags = EVFILT_READ;
  EV_SET(&ev->change, ev->fd, EVFILT_READ, EV_ADD, 0, 0, ev);
  if(kevent(kqueuefd, &ev->change, 1, nullptr, 0, nullptr) == -1)
  {
    llarp::LogError("Failed to add event: ", strerror(errno));
    delete ev;
    return false;
  }
  if(w)
  {
    ev->flags |= EVFILT_WRITE;
    EV_SET(&ev->change, ev->fd, EVFILT_WRITE, EV_ADD, 0, 0, ev);
    if(kevent(kqueuefd, &ev->change, 1, nullptr, 0, nullptr) == -1)
    {
      llarp::LogError("Failed to add event: ", strerror(errno));
      delete ev;
      return false;
    }
  }
  handlers.emplace_back(ev);
  return true;
}

bool
llarp_kqueue_loop::udp_close(llarp_udp_io* l)
{
  bool ret      = false;
  auto listener = static_cast< llarp::udp_listener* >(l->impl);
  if(listener)
  {
    // printf("Calling close_ev for [%x] fd[%d]\n", listener, listener->fd);
    ret = close_ev(listener);
    // remove handler
    auto itr = handlers.begin();
    while(itr != handlers.end())
    {
      if(itr->get() == listener)
      {
        itr = handlers.erase(itr);
        ret = true;
      }
      else
        ++itr;
    }
    l->impl = nullptr;
  }
  return ret;
}

void
llarp_kqueue_loop::stop()
{
  auto itr = handlers.begin();
  while(itr != handlers.end())
  {
    close_ev(itr->get());
    itr = handlers.erase(itr);
  }

  if(kqueuefd != -1)
    ::close(kqueuefd);

  kqueuefd = -1;
}
