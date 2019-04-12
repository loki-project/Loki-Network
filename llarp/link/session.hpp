#ifndef LLARP_LINK_SESSION_HPP
#define LLARP_LINK_SESSION_HPP

#include <crypto/types.hpp>
#include <net/net.hpp>
#include <router_contact.hpp>
#include <util/types.hpp>

#include <functional>

namespace llarp
{
  struct LinkIntroMessage;
  struct ILinkMessage;
  struct ILinkLayer;
  struct ILinkSession : public util::IStateful
  {
    virtual ~ILinkSession(){};

    /// hook for utp for when we have established a connection
    virtual void
    OnLinkEstablished(ILinkLayer *p) = 0;

    /// called every event loop tick
    virtual void
    Pump() = 0;

    /// called every timer tick
    virtual void Tick(llarp_time_t) = 0;

    /// send a message buffer to the remote endpoint
    virtual bool
    SendMessageBuffer(const llarp_buffer_t &) = 0;

    /// start the connection
    virtual void
    Start() = 0;

    virtual void
    Close() = 0;

    /// send a keepalive to the remote endpoint
    virtual bool
    SendKeepAlive() = 0;

    /// return true if we are established
    virtual bool
    IsEstablished() = 0;

    /// return true if this session has timed out
    virtual bool
    TimedOut(llarp_time_t now) const = 0;

    /// get remote public identity key
    virtual PubKey
    GetPubKey() const = 0;

    /// get remote address
    virtual Addr
    GetRemoteEndpoint() const = 0;

    // get remote rc
    virtual RouterContact
    GetRemoteRC() const = 0;

    /// handle a valid LIM
    std::function< bool(const LinkIntroMessage *msg) > GotLIM;

    /// send queue current blacklog
    virtual size_t
    SendQueueBacklog() const = 0;

    /// get parent link layer
    virtual ILinkLayer *
    GetLinkLayer() const = 0;

    /// renegotiate session when we have a new RC locally
    virtual bool
    RenegotiateSession() = 0;

    /// return true if we should send an explicit keepalive message
    virtual bool
    ShouldPing() const = 0;
  };
}  // namespace llarp

#endif
