#include <messages/exit.hpp>

#include <crypto/crypto.hpp>
#include <routing/handler.hpp>

namespace llarp
{
  namespace routing
  {
    bool
    RejectExitMessage::BEncode(llarp_buffer_t* buf) const
    {
      if(!bencode_start_dict(buf))
        return false;
      if(!BEncodeWriteDictMsgType(buf, "A", "J"))
        return false;
      if(!BEncodeWriteDictInt("B", B, buf))
        return false;
      if(!BEncodeWriteDictList("R", R, buf))
        return false;
      if(!BEncodeWriteDictInt("S", S, buf))
        return false;
      if(!BEncodeWriteDictInt("T", T, buf))
        return false;
      if(!BEncodeWriteDictInt("V", version, buf))
        return false;
      if(!BEncodeWriteDictEntry("Y", Y, buf))
        return false;
      if(!BEncodeWriteDictEntry("Z", Z, buf))
        return false;
      return bencode_end(buf);
    }

    bool
    RejectExitMessage::DecodeKey(const llarp_buffer_t& k, llarp_buffer_t* buf)
    {
      bool read = false;
      if(!BEncodeMaybeReadDictInt("B", B, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictList("R", R, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("S", S, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("T", T, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("V", version, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("Y", Y, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("Z", Z, read, k, buf))
        return false;
      return read;
    }

    RejectExitMessage&
    RejectExitMessage::operator=(const RejectExitMessage& other)
    {
      B       = other.B;
      R       = other.R;
      S       = other.S;
      T       = other.T;
      version = other.version;
      Y       = other.Y;
      Z       = other.Z;
      return *this;
    }

    bool
    RejectExitMessage::Sign(llarp::Crypto* c, const llarp::SecretKey& sk)
    {
      std::array< byte_t, 512 > tmp;
      llarp_buffer_t buf(tmp);
      Z.Zero();
      Y.Randomize();
      if(!BEncode(&buf))
        return false;
      buf.sz = buf.cur - buf.base;
      return c->sign(Z, sk, buf);
    }

    bool
    RejectExitMessage::Verify(llarp::Crypto* c, const llarp::PubKey& pk) const
    {
      std::array< byte_t, 512 > tmp;
      llarp_buffer_t buf(tmp);
      RejectExitMessage copy;
      copy = *this;
      copy.Z.Zero();
      if(!copy.BEncode(&buf))
        return false;
      buf.sz = buf.cur - buf.base;
      return c->verify(pk, buf, Z);
    }

    bool
    RejectExitMessage::HandleMessage(IMessageHandler* h,
                                     AbstractRouter* r) const
    {
      return h->HandleRejectExitMessage(*this, r);
    }

  }  // namespace routing
}  // namespace llarp
