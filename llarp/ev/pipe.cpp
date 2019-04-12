#include <ev/pipe.hpp>

#include <unistd.h>
#include <fcntl.h>

llarp_ev_pkt_pipe::llarp_ev_pkt_pipe(llarp_ev_loop_ptr loop)
    : llarp::ev_io(-1, new LosslessWriteQueue_t()), m_Loop(loop)
{
}

bool
llarp_ev_pkt_pipe::Start()
{
#if defined(_WIN32)
  llarp::LogError("llarp_ev_pkt_pipe not supported on win32");
  return false;
#else
  int _fds[2];
#if defined(__APPLE__)
  if(pipe(_fds) == -1
     && fcntl(_fds[0], F_SETFL, fcntl(_fds[0], F_GETFL) | O_NONBLOCK))
  {
    return false;
  }
#else
#if defined(__linux__) || defined(__FreeBSD__)
  if(pipe2(_fds, O_DIRECT | O_NONBLOCK) == -1)
#else
  // non-linux reeeeeeeee
  if(pipe2(_fds, O_SYNC | O_NONBLOCK) == -1)
#endif
  {
    return false;
  }
#endif
  fd      = _fds[0];
  writefd = _fds[1];
  return true;
#endif
}

int
llarp_ev_pkt_pipe::read(byte_t* pkt, size_t sz)
{
  auto res = ::read(fd, pkt, sz);
  if(res <= 0)
    return res;
  llarp::LogDebug("read ", res, " on pipe");
  llarp_buffer_t buf(pkt, res);
  OnRead(buf);
  return res;
}

ssize_t
llarp_ev_pkt_pipe::do_write(void* buf, size_t sz)
{
  llarp::LogInfo("pipe write ", sz);
  return ::write(writefd, buf, sz);
}

bool
llarp_ev_pkt_pipe::Write(const llarp_buffer_t& pkt)
{
  const ssize_t sz = pkt.sz;
  llarp::LogDebug("write ", sz, " on pipe");
  if(::write(writefd, pkt.base, pkt.sz) != sz)
  {
    llarp::LogDebug("queue write ", pkt.sz);
    return queue_write(pkt.base, pkt.sz);
  }
  return true;
}

bool
llarp_ev_pkt_pipe::tick()
{
  llarp::ev_io::flush_write();
  return true;
}
