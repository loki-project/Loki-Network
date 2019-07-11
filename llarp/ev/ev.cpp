#include <ev/ev.h>
#include <util/logic.hpp>
#include <util/mem.hpp>
#include <util/string_view.hpp>
#include <net/net_addr.hpp>

#include <cstddef>
#include <cstring>

// We libuv now
#ifndef _WIN32
#include <ev/ev_libuv.hpp>
#elif defined(_WIN32) || defined(_WIN64) || defined(__NT__)
#define SHUT_RDWR SD_BOTH
#include <ev/ev_win32.hpp>
#else
#error No async event loop for your platform, port libuv to your operating system
#endif

llarp_ev_loop_ptr
llarp_make_ev_loop()
{
#ifndef _WIN32
  llarp_ev_loop_ptr r = std::make_shared< libuv::Loop >();
#elif defined(_WIN32) || defined(_WIN64) || defined(__NT__)
  llarp_ev_loop_ptr r = std::make_shared< llarp_win32_loop >();
#else
#error no event loop subclass
#endif
  r->init();
  r->update_time();
  return r;
}

void
llarp_ev_loop_run_single_process(llarp_ev_loop_ptr ev,
                                 struct llarp_threadpool *tp,
                                 std::shared_ptr< llarp::Logic > logic)
{
  while(ev->running())
  {
    ev->update_time();
    ev->tick(EV_TICK_INTERVAL);
    if(ev->running())
    {
      ev->update_time();
      logic->tick_async(ev->time_now());
      llarp_threadpool_tick(tp);
    }
    llarp::LogContext::Instance().logStream->Tick(ev->time_now());
  }
  ev->stopped();
}

int
llarp_ev_add_udp(struct llarp_ev_loop *ev, struct llarp_udp_io *udp,
                 const struct sockaddr *src)
{
  udp->parent = ev;
  if(ev->udp_listen(udp, src))
    return 0;
  return -1;
}

int
llarp_ev_close_udp(struct llarp_udp_io *udp)
{
  if(udp->parent->udp_close(udp))
    return 0;
  return -1;
}

llarp_time_t
llarp_ev_loop_time_now_ms(const llarp_ev_loop_ptr &loop)
{
  if(loop)
    return loop->time_now();
  return llarp::time_now_ms();
}

void
llarp_ev_loop_stop(const llarp_ev_loop_ptr &loop)
{
  loop->stop();
}

int
llarp_ev_udp_sendto(struct llarp_udp_io *udp, const sockaddr *to,
                    const llarp_buffer_t &buf)
{
  return udp->sendto(udp, to, buf.base, buf.sz);
}

bool
llarp_ev_add_tun(struct llarp_ev_loop *loop, struct llarp_tun_io *tun)
{
  // llarp::LogInfo("ev creating tunnel ", tun->ifaddr, " on ", tun->ifname);
  if(strcmp(tun->ifaddr, "") == 0 || strcmp(tun->ifaddr, "auto") == 0)
  {
    std::string ifaddr = llarp::findFreePrivateRange();
    auto pos           = ifaddr.find("/");
    if(pos == std::string::npos)
    {
      llarp::LogWarn("Auto ifaddr didn't return a netmask: ", ifaddr);
      return false;
    }
    int num;
    std::string part = ifaddr.substr(pos + 1);
#if defined(ANDROID) || defined(RPI)
    num = atoi(part.c_str());
#else
    num = std::stoi(part);
#endif
    if(num <= 0)
    {
      llarp::LogError("bad ifaddr netmask value: ", ifaddr);
      return false;
    }
    tun->netmask           = num;
    const std::string addr = ifaddr.substr(0, pos);
    std::copy_n(addr.begin(), std::min(sizeof(tun->ifaddr), addr.size()),
                tun->ifaddr);
    llarp::LogInfo("IfAddr autodetect: ", tun->ifaddr, "/", tun->netmask);
  }
  if(strcmp(tun->ifname, "") == 0 || strcmp(tun->ifname, "auto") == 0)
  {
    std::string ifname = llarp::findFreeLokiTunIfName();
    std::copy_n(ifname.begin(), std::min(sizeof(tun->ifname), ifname.size()),
                tun->ifname);
    llarp::LogInfo("IfName autodetect: ", tun->ifname);
  }
  llarp::LogDebug("Tun Interface will use the following settings:");
  llarp::LogDebug("IfAddr: ", tun->ifaddr);
  llarp::LogDebug("IfName: ", tun->ifname);
  llarp::LogDebug("IfNMsk: ", tun->netmask);
#ifndef _WIN32
  return loop->tun_listen(tun);
#else
  UNREFERENCED_PARAMETER(loop);
  auto dev  = new win32_tun_io(tun);
  tun->impl = dev;
  // We're not even going to add this to the socket event loop
  if(dev)
  {
    dev->setup();
    return dev->add_ev();  // start up tun and add to event queue
  }
#endif
  llarp::LogWarn("Loop could not create tun");
  return false;
}

bool
llarp_ev_tun_async_write(struct llarp_tun_io *tun, const llarp_buffer_t &buf)
{
  if(buf.sz > EV_WRITE_BUF_SZ)
  {
    llarp::LogWarn("packet too big, ", buf.sz, " > ", EV_WRITE_BUF_SZ);
    return false;
  }
#ifndef _WIN32
  return tun->writepkt(tun, buf.base, buf.sz);
#else
  return static_cast< win32_tun_io * >(tun->impl)->queue_write(buf.base,
                                                               buf.sz);
#endif
}

bool
llarp_tcp_conn_async_write(struct llarp_tcp_conn *conn, const llarp_buffer_t &b)
{
  ManagedBuffer buf{b};

  size_t sz          = buf.underlying.sz;
  buf.underlying.cur = buf.underlying.base;
  while(sz > EV_WRITE_BUF_SZ)
  {
    ssize_t amount = conn->write(conn, buf.underlying.cur, EV_WRITE_BUF_SZ);
    if(amount <= 0)
    {
      llarp::LogError("write underrun");
      return false;
    }
    buf.underlying.cur += amount;
    sz -= amount;
  }
  return conn->write(conn, buf.underlying.cur, sz) > 0;
}

void
llarp_tcp_async_try_connect(struct llarp_ev_loop *loop,
                            struct llarp_tcp_connecter *tcp)
{
  tcp->loop = loop;
  llarp::string_view addr_str, port_str;
  // try parsing address
  const char *begin = tcp->remote;
  const char *ptr   = strstr(tcp->remote, ":");
  // get end of address

  if(ptr == nullptr)
  {
    llarp::LogError("bad address: ", tcp->remote);
    if(tcp->error)
      tcp->error(tcp);
    return;
  }
  const char *end = ptr;
  while(*end && ((end - begin) < static_cast< ptrdiff_t >(sizeof tcp->remote)))
  {
    ++end;
  }
  addr_str = llarp::string_view(begin, ptr - begin);
  ++ptr;
  port_str = llarp::string_view(ptr, end - ptr);
  // actually parse address
  llarp::Addr addr(addr_str, port_str);

  if(!loop->tcp_connect(tcp, addr))
  {
    llarp::LogError("async connect failed");
    if(tcp->error)
      tcp->error(tcp);
  }
}

bool
llarp_tcp_serve(struct llarp_ev_loop *loop, struct llarp_tcp_acceptor *tcp,
                const struct sockaddr *bindaddr)
{
  tcp->loop = loop;
  return loop->tcp_listen(tcp, bindaddr);
}

void
llarp_tcp_acceptor_close(struct llarp_tcp_acceptor *tcp)
{
  tcp->close(tcp);
}

void
llarp_tcp_conn_close(struct llarp_tcp_conn *conn)
{
  conn->close(conn);
}

namespace llarp
{
  bool
  tcp_conn::tick()
  {
    if(_shouldClose)
    {
      if(tcp.closed)
        tcp.closed(&tcp);
      ::shutdown(fd, SHUT_RDWR);
      return false;
    }
    if(tcp.tick)
      tcp.tick(&tcp);
    return true;
  }

}  // namespace llarp
