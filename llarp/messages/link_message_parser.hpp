#ifndef LLARP_LINK_MESSAGE_PARSER_HPP
#define LLARP_LINK_MESSAGE_PARSER_HPP

#include <router_id.hpp>
#include <util/bencode.h>

#include <memory>

namespace llarp
{
  struct AbstractRouter;
  struct ILinkMessage;
  struct ILinkSession;

  struct InboundMessageParser
  {
    InboundMessageParser(AbstractRouter* router);
    ~InboundMessageParser();
    dict_reader reader;

    static bool
    OnKey(dict_reader* r, llarp_buffer_t* buf);

    /// start processig message from a link session
    bool
    ProcessFrom(ILinkSession* from, const llarp_buffer_t& buf);

    /// called when the message is fully read
    /// return true when the message was accepted otherwise returns false
    bool
    MessageDone();

    /// resets internal state
    void
    Reset();

   private:
    RouterID
    GetCurrentFrom();

   private:
    bool firstkey;
    AbstractRouter* router;
    ILinkSession* from;
    ILinkMessage* msg;

    struct msg_holder_t;
    msg_holder_t *holder;
  };
}  // namespace llarp
#endif
