#include <dht/context.hpp>
#include <dht/messages/gotrouter.hpp>

#include <path/path.hpp>
#include <router/abstractrouter.hpp>

namespace llarp
{
  namespace dht
  {
    GotRouterMessage::~GotRouterMessage()
    {
    }

    bool
    GotRouterMessage::BEncode(llarp_buffer_t *buf) const
    {
      if(!bencode_start_dict(buf))
        return false;

      // message type
      if(!BEncodeWriteDictMsgType(buf, "A", "S"))
        return false;

      if(K)
      {
        if(!BEncodeWriteDictEntry("K", *K.get(), buf))
          return false;
      }

      // near
      if(N.size())
      {
        if(!BEncodeWriteDictList("N", N, buf))
          return false;
      }

      if(!BEncodeWriteDictList("R", R, buf))
        return false;

      // txid
      if(!BEncodeWriteDictInt("T", txid, buf))
        return false;

      // version
      if(!BEncodeWriteDictInt("V", version, buf))
        return false;

      return bencode_end(buf);
    }

    bool
    GotRouterMessage::DecodeKey(const llarp_buffer_t &key, llarp_buffer_t *val)
    {
      if(key == "K")
      {
        if(K)  // duplicate key?
          return false;
        K.reset(new dht::Key_t());
        return K->BDecode(val);
      }
      if(key == "N")
      {
        return BEncodeReadList(N, val);
      }
      if(key == "R")
      {
        return BEncodeReadList(R, val);
      }
      if(key == "T")
      {
        return bencode_read_integer(val, &txid);
      }
      bool read = false;
      if(!BEncodeMaybeReadVersion("V", version, LLARP_PROTO_VERSION, read, key,
                                  val))
        return false;

      return read;
    }

    bool
    GotRouterMessage::HandleMessage(
        llarp_dht_context *ctx,
        __attribute__((unused))
        std::vector< std::unique_ptr< IMessage > > &replies) const
    {
      auto &dht = *ctx->impl;
      if(relayed)
      {
        auto pathset =
            ctx->impl->GetRouter()->pathContext().GetLocalPathSet(pathID);
        return pathset && pathset->HandleGotRouterMessage(this);
      }
      // not relayed
      const TXOwner owner(From, txid);

      if(dht.pendingExploreLookups().HasPendingLookupFrom(owner))
      {
        if(N.size() == 0)
          dht.pendingExploreLookups().NotFound(owner, K);
        else
        {
          dht.pendingExploreLookups().Found(owner, From.as_array(), N);
        }
        return true;
      }
      // not explore lookup
      if(dht.pendingRouterLookups().HasPendingLookupFrom(owner))
      {
        if(R.size() == 0)
          dht.pendingRouterLookups().NotFound(owner, K);
        else
          dht.pendingRouterLookups().Found(owner, R[0].pubkey, R);
        return true;
      }
      llarp::LogWarn("Unwarranted GRM from ", From, " txid=", txid);
      return false;
    }
  }  // namespace dht
}  // namespace llarp
