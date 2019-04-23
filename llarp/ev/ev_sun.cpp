#include <ev/ev_sun.hpp>

namespace llarp
{
  int
  tcp_conn::read(byte_t* buf, size_t sz)
  {
    if(_shouldClose)
      return -1;

    ssize_t amount = ::read(fd, buf, sz);

    if(amount > 0)
    {
      if(tcp.read)
        tcp.read(&tcp, llarp_buffer_t(buf, amount));
    }
    else if(amount < 0)
    {
      // error
      _shouldClose = true;
      errno        = 0;
      return -1;
    }
    return 0;
  }

  void
  tcp_conn::flush_write()
  {
    connected();
    ev_io::flush_write();
  }

  ssize_t
  tcp_conn::do_write(void* buf, size_t sz)
  {
    if(_shouldClose)
      return -1;
    // pretty much every UNIX system still extant, _including_ solaris
    // (on both sides of the fork) can ignore SIGPIPE....except
    // the other vendored systems... -rick
    return ::send(fd, buf, sz, MSG_NOSIGNAL);  // ignore sigpipe
  }

  void
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
      llarp::LogDebug("connected immedidately");
      connected();
    }
    else if(errno == EINPROGRESS)
    {
      // in progress
      llarp::LogDebug("connect in progress");
      errno = 0;
      return;
    }
    else if(_conn->error)
    {
      // wtf?
      llarp::LogError("error connecting ", strerror(errno));
      _conn->error(_conn);
      errno = 0;
    }
  }

  int
  tcp_serv::read(byte_t*, size_t)
  {
    int new_fd = ::accept(fd, nullptr, nullptr);
    if(new_fd == -1)
    {
      llarp::LogError("failed to accept on ", fd, ":", strerror(errno));
      return -1;
    }
    // build handler
    llarp::tcp_conn* connimpl = new tcp_conn(loop, new_fd);
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
    llarp_buffer_t b;
    b.base = buf;
    b.cur  = b.base;
    sockaddr_in6 src;
    socklen_t slen = sizeof(sockaddr_in6);
    sockaddr* addr = (sockaddr*)&src;
    ssize_t ret    = ::recvfrom(fd, b.base, sz, 0, addr, &slen);
    if(ret < 0)
    {
      errno = 0;
      return -1;
    }
    if(static_cast< size_t >(ret) > sz)
      return -1;
    b.sz = ret;
    udp->recvfrom(udp, addr, ManagedBuffer{b});
    return ret;
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
    ssize_t sent = ::sendto(fd, data, sz, SOCK_NONBLOCK, to, slen);
    if(sent == -1)
    {
      llarp::LogWarn(strerror(errno));
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

  bool
  tun::tick()
  {
    if(t->tick)
      t->tick(t);
    flush_write();
    return true;
  }

  void
  tun::flush_write()
  {
    if(t->before_write)
      t->before_write(t);
    ev_io::flush_write();
  }

  int
  tun::read(byte_t* buf, size_t sz)
  {
    ssize_t ret = tuntap_read(tunif, buf, sz);
    if(ret > 0 && t->recvpkt)
    {
      // does not have pktinfo
      t->recvpkt(t, llarp_buffer_t(buf, ret));
    }
    return ret;
  }

  bool
  tun::setup()
  {
    llarp::LogDebug("set ifname to ", t->ifname);
    strncpy(tunif->if_name, t->ifname, sizeof(tunif->if_name));
    if(tuntap_start(tunif, TUNTAP_MODE_TUNNEL, 0) == -1)
    {
      llarp::LogWarn("failed to start interface");
      return false;
    }
    if(tuntap_up(tunif) == -1)
    {
      llarp::LogWarn("failed to put interface up: ", strerror(errno));
      return false;
    }
    if(tuntap_set_ip(tunif, t->ifaddr, t->ifaddr, t->netmask) == -1)
    {
      llarp::LogWarn("failed to set ip");
      return false;
    }
    fd = tunif->tun_fd;
    if(fd == -1)
      return false;
    // set non blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1)
      return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
  }

};  // namespace llarp

bool
llarp_poll_loop::tcp_connect(struct llarp_tcp_connecter* tcp,
                             const sockaddr* remoteaddr)
{
  // create socket
  int fd = ::socket(remoteaddr->sa_family, SOCK_STREAM, 0);
  if(fd == -1)
    return false;
  // set non blocking
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
  llarp::tcp_conn* conn = new llarp::tcp_conn(this, fd, remoteaddr, tcp);
  add_ev(conn, true);
  conn->connect();
  return true;
}

llarp::ev_io*
llarp_poll_loop::bind_tcp(llarp_tcp_acceptor* tcp, const sockaddr* bindaddr)
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
  return new llarp::tcp_serv(this, fd, tcp);
}

bool
llarp_poll_loop::udp_listen(llarp_udp_io* l, const sockaddr* src)
{
  auto ev = create_udp(l, src);
  if(ev)
    l->fd = ev->fd;
  return ev && add_ev(ev, false);
}

bool
llarp_poll_loop::running() const
{
  return upollfd != nullptr;
}

bool
llarp_poll_loop::init()
{
  if(!upollfd)
    upollfd = upoll_create(1);  // why do we return false? (see ev_epoll.cpp)
  return false;
}

int
llarp_poll_loop::tick(int ms)
{
  upoll_event_t events[1024];
  int result;
  result     = upoll_wait(upollfd, events, 1024, ms);
  bool didIO = false;
  if(result > 0)
  {
    int idx = 0;
    while(idx < result)
    {
      llarp::ev_io* ev = static_cast< llarp::ev_io* >(events[idx].data.ptr);
      if(ev)
      {
        llarp::LogDebug(idx, " of ", result, " on ", ev->fd,
                        " events=", std::to_string(events[idx].events));
        if(events[idx].events & UPOLLERR && errno)
        {
          IO([&]() -> ssize_t {
            llarp::LogDebug("upoll error");
            ev->error();
            return 0;
          });
        }
        else
        {
          // write THEN READ don't revert me
          if(events[idx].events & UPOLLOUT)
          {
            IO([&]() -> ssize_t {
              llarp::LogDebug("upoll out");
              ev->flush_write();
              return 0;
            });
          }
          if(events[idx].events & UPOLLIN)
          {
            ssize_t amount = IO([&]() -> ssize_t {
              llarp::LogDebug("upoll in");
              return ev->read(readbuf, sizeof(readbuf));
            });
            if(amount > 0)
              didIO = true;
          }
        }
      }
      ++idx;
    }
  }
  if(result != -1)
    tick_listeners();
  /// if we didn't get an io events we sleep to avoid 100% cpu use
  if(!didIO)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return result;
}

int
llarp_poll_loop::run()
{
  upoll_event_t events[1024];
  int result;
  do
  {
    result = upoll_wait(upollfd, events, 1024, EV_TICK_INTERVAL);
    if(result > 0)
    {
      int idx = 0;
      while(idx < result)
      {
        llarp::ev_io* ev = static_cast< llarp::ev_io* >(events[idx].data.ptr);
        if(ev)
        {
          if(events[idx].events & UPOLLERR)
          {
            ev->error();
          }
          else
          {
            if(events[idx].events & UPOLLIN)
            {
              ev->read(readbuf, sizeof(readbuf));
            }
            if(events[idx].events & UPOLLOUT)
            {
              ev->flush_write();
            }
          }
        }
        ++idx;
      }
    }
    if(result != -1)
      tick_listeners();
  } while(upollfd);
  return result;
}

int
llarp_poll_loop::udp_bind(const sockaddr* addr)
{
  socklen_t slen;
  switch(addr->sa_family)
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
  if(bind(fd, addr, slen) == -1)
  {
    perror("bind()");
    close(fd);
    return -1;
  }

  return fd;
}

bool
llarp_poll_loop::close_ev(llarp::ev_io* ev)
{
  return upoll_ctl(upollfd, UPOLL_CTL_DEL, ev->fd, nullptr) != -1;
}

llarp::ev_io*
llarp_poll_loop::create_tun(llarp_tun_io* tun)
{
  llarp::tun* t = new llarp::tun(tun, shared_from_this());
  if(t->setup())
  {
    return t;
  }
  delete t;
  return nullptr;
}

llarp::ev_io*
llarp_poll_loop::create_udp(llarp_udp_io* l, const sockaddr* src)
{
  int fd = udp_bind(src);
  if(fd == -1)
    return nullptr;
  llarp::ev_io* listener = new llarp::udp_listener(fd, l);
  l->impl                = listener;
  return listener;
}

bool
llarp_poll_loop::add_ev(llarp::ev_io* e, bool write)
{
  upoll_event_t ev;
  ev.data.ptr = e;
  ev.events   = UPOLLIN | UPOLLERR;
  if(write)
    ev.events |= UPOLLOUT;
  if(upoll_ctl(upollfd, UPOLL_CTL_ADD, e->fd, &ev) == -1)
  {
    delete e;
    return false;
  }
  handlers.emplace_back(e);
  return true;
}

bool
llarp_poll_loop::udp_close(llarp_udp_io* l)
{
  bool ret                      = false;
  llarp::udp_listener* listener = static_cast< llarp::udp_listener* >(l->impl);
  if(listener)
  {
    close_ev(listener);
    // remove handler
    auto itr = handlers.begin();
    while(itr != handlers.end())
    {
      if(itr->get() == listener)
        itr = handlers.erase(itr);
      else
        ++itr;
    }
    l->impl = nullptr;
    ret     = true;
  }
  return ret;
}

void
llarp_poll_loop::stop()
{
  // close all handlers before closing the upoll fd
  auto itr = handlers.begin();
  while(itr != handlers.end())
  {
    close_ev(itr->get());
    itr = handlers.erase(itr);
  }

  if(upollfd)
    upoll_destroy(upollfd);
  upollfd = nullptr;
}
