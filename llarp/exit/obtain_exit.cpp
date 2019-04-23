#include <messages/exit.hpp>

#include <crypto/crypto.hpp>
#include <routing/handler.hpp>

namespace llarp
{
  namespace routing
  {
    bool
    ObtainExitMessage::Sign(llarp::Crypto* c, const llarp::SecretKey& sk)
    {
      std::array< byte_t, 1024 > tmp;
      llarp_buffer_t buf(tmp);
      I = seckey_topublic(sk);
      Z.Zero();
      if(!BEncode(&buf))
      {
        return false;
      }
      buf.sz = buf.cur - buf.base;
      return c->sign(Z, sk, buf);
    }

    bool
    ObtainExitMessage::Verify(llarp::Crypto* c) const
    {
      std::array< byte_t, 1024 > tmp;
      llarp_buffer_t buf(tmp);
      ObtainExitMessage copy;
      copy = *this;
      copy.Z.Zero();
      if(!copy.BEncode(&buf))
      {
        return false;
      }
      // rewind buffer
      buf.sz = buf.cur - buf.base;
      return c->verify(I, buf, Z);
    }

    bool
    ObtainExitMessage::BEncode(llarp_buffer_t* buf) const
    {
      if(!bencode_start_dict(buf))
        return false;
      if(!BEncodeWriteDictMsgType(buf, "A", "O"))
        return false;
      if(!BEncodeWriteDictArray("B", B, buf))
        return false;
      if(!BEncodeWriteDictInt("E", E, buf))
        return false;
      if(!BEncodeWriteDictEntry("I", I, buf))
        return false;
      if(!BEncodeWriteDictInt("S", S, buf))
        return false;
      if(!BEncodeWriteDictInt("T", T, buf))
        return false;
      if(!BEncodeWriteDictInt("V", version, buf))
        return false;
      if(!BEncodeWriteDictArray("W", W, buf))
        return false;
      if(!BEncodeWriteDictInt("X", X, buf))
        return false;
      if(!BEncodeWriteDictEntry("Z", Z, buf))
        return false;
      return bencode_end(buf);
    }

    bool
    ObtainExitMessage::DecodeKey(const llarp_buffer_t& k, llarp_buffer_t* buf)
    {
      bool read = false;
      if(!BEncodeMaybeReadDictList("B", B, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("E", E, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("I", I, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("S", S, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("T", T, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("V", version, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictList("W", W, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("X", X, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("Z", Z, read, k, buf))
        return false;
      return read;
    }

    bool
    ObtainExitMessage::HandleMessage(IMessageHandler* h,
                                     AbstractRouter* r) const
    {
      return h->HandleObtainExitMessage(*this, r);
    }

  }  // namespace routing
}  // namespace llarp
