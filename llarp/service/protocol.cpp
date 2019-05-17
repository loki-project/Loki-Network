#include <service/protocol.hpp>
#include <path/path.hpp>
#include <routing/handler.hpp>
#include <util/buffer.hpp>
#include <util/logic.hpp>
#include <util/mem.hpp>

namespace llarp
{
  namespace service
  {
    ProtocolMessage::ProtocolMessage()
    {
      tag.Zero();
    }

    ProtocolMessage::ProtocolMessage(const ConvoTag& t) : tag(t)
    {
    }

    ProtocolMessage::~ProtocolMessage()
    {
    }

    void
    ProtocolMessage::PutBuffer(const llarp_buffer_t& buf)
    {
      payload.resize(buf.sz);
      memcpy(payload.data(), buf.base, buf.sz);
    }

    void
    ProtocolMessage::ProcessAsync(std::shared_ptr< ProtocolMessage > self)
    {
      if(!self->handler->HandleDataMessage(self->srcPath, self))
        LogWarn("failed to handle data message from ", self->srcPath);
    }

    bool
    ProtocolMessage::DecodeKey(const llarp_buffer_t& k, llarp_buffer_t* buf)
    {
      bool read = false;
      if(!BEncodeMaybeReadDictInt("a", proto, read, k, buf))
        return false;
      if(k == "d")
      {
        llarp_buffer_t strbuf;
        if(!bencode_read_string(buf, &strbuf))
          return false;
        PutBuffer(strbuf);
        return true;
      }
      if(!BEncodeMaybeReadDictEntry("i", introReply, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("s", sender, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictEntry("t", tag, read, k, buf))
        return false;
      if(!BEncodeMaybeReadDictInt("v", version, read, k, buf))
        return false;
      return read;
    }

    bool
    ProtocolMessage::BEncode(llarp_buffer_t* buf) const
    {
      if(!bencode_start_dict(buf))
        return false;
      if(!BEncodeWriteDictInt("a", proto, buf))
        return false;
      if(!bencode_write_bytestring(buf, "d", 1))
        return false;
      if(!bencode_write_bytestring(buf, payload.data(), payload.size()))
        return false;
      if(!BEncodeWriteDictEntry("i", introReply, buf))
        return false;
      if(!BEncodeWriteDictEntry("s", sender, buf))
        return false;
      if(!tag.IsZero())
      {
        if(!BEncodeWriteDictEntry("t", tag, buf))
          return false;
      }
      if(!BEncodeWriteDictInt("v", version, buf))
        return false;
      return bencode_end(buf);
    }

    ProtocolFrame::~ProtocolFrame()
    {
    }

    bool
    ProtocolFrame::BEncode(llarp_buffer_t* buf) const
    {
      if(!bencode_start_dict(buf))
        return false;

      if(!BEncodeWriteDictMsgType(buf, "A", "H"))
        return false;
      if(!C.IsZero())
      {
        if(!BEncodeWriteDictEntry("C", C, buf))
          return false;
      }
      if(D.size() > 0)
      {
        if(!BEncodeWriteDictEntry("D", D, buf))
          return false;
      }
      if(!BEncodeWriteDictEntry("F", F, buf))
        return false;
      if(!N.IsZero())
      {
        if(!BEncodeWriteDictEntry("N", N, buf))
          return false;
      }
      if(R)
      {
        if(!BEncodeWriteDictInt("R", R, buf))
          return false;
      }
      if(!T.IsZero())
      {
        if(!BEncodeWriteDictEntry("T", T, buf))
          return false;
      }
      if(!BEncodeWriteDictInt("V", version, buf))
        return false;
      if(!BEncodeWriteDictEntry("Z", Z, buf))
        return false;
      return bencode_end(buf);
    }

    bool
    ProtocolFrame::DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* val)
    {
      bool read = false;
      if(key == "A")
      {
        llarp_buffer_t strbuf;
        if(!bencode_read_string(val, &strbuf))
          return false;
        if(strbuf.sz != 1)
          return false;
        return *strbuf.cur == 'H';
      }
      if(!BEncodeMaybeReadDictEntry("D", D, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictEntry("F", F, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictEntry("C", C, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictEntry("N", N, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictInt("S", S, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictInt("R", R, read, key, val))
        return false;
      if(!BEncodeMaybeReadDictEntry("T", T, read, key, val))
        return false;
      if(!BEncodeMaybeReadVersion("V", version, LLARP_PROTO_VERSION, read, key,
                                  val))
        return false;
      if(!BEncodeMaybeReadDictEntry("Z", Z, read, key, val))
        return false;
      return read;
    }

    bool
    ProtocolFrame::DecryptPayloadInto(Crypto* crypto,
                                      const SharedSecret& sharedkey,
                                      ProtocolMessage& msg) const
    {
      Encrypted_t tmp = D;
      auto buf        = tmp.Buffer();
      crypto->xchacha20(*buf, sharedkey, N);
      return msg.BDecode(buf);
    }

    bool
    ProtocolFrame::Sign(Crypto* crypto, const Identity& localIdent)
    {
      Z.Zero();
      std::array< byte_t, MAX_PROTOCOL_MESSAGE_SIZE > tmp;
      llarp_buffer_t buf(tmp);
      // encode
      if(!BEncode(&buf))
      {
        LogError("message too big to encode");
        return false;
      }
      // rewind
      buf.sz  = buf.cur - buf.base;
      buf.cur = buf.base;
      // sign
      return localIdent.Sign(crypto, Z, buf);
    }

    bool
    ProtocolFrame::EncryptAndSign(Crypto* crypto, const ProtocolMessage& msg,
                                  const SharedSecret& sessionKey,
                                  const Identity& localIdent)
    {
      std::array< byte_t, MAX_PROTOCOL_MESSAGE_SIZE > tmp;
      llarp_buffer_t buf(tmp);
      // encode message
      if(!msg.BEncode(&buf))
      {
        LogError("message too big to encode");
        return false;
      }
      // rewind
      buf.sz  = buf.cur - buf.base;
      buf.cur = buf.base;
      // encrypt
      crypto->xchacha20(buf, sessionKey, N);
      // put encrypted buffer
      D = buf;
      // zero out signature
      Z.Zero();
      llarp_buffer_t buf2(tmp);
      // encode frame
      if(!BEncode(&buf2))
      {
        LogError("frame too big to encode");
        DumpBuffer(buf2);
        return false;
      }
      // rewind
      buf2.sz  = buf2.cur - buf2.base;
      buf2.cur = buf2.base;
      // sign
      if(!localIdent.Sign(crypto, Z, buf2))
      {
        LogError("failed to sign? wtf?!");
        return false;
      }
      return true;
    }

    struct AsyncFrameDecrypt
    {
      Crypto* crypto;
      Logic* logic;
      std::shared_ptr< ProtocolMessage > msg;
      const Identity& m_LocalIdentity;
      IDataHandler* handler;
      const ProtocolFrame frame;
      const Introduction fromIntro;

      AsyncFrameDecrypt(Logic* l, Crypto* c, const Identity& localIdent,
                        IDataHandler* h,
                        const std::shared_ptr< ProtocolMessage >& m,
                        const ProtocolFrame& f, const Introduction& recvIntro)
          : crypto(c)
          , logic(l)
          , msg(m)
          , m_LocalIdentity(localIdent)
          , handler(h)
          , frame(f)
          , fromIntro(recvIntro)
      {
      }

      static void
      Work(void* user)
      {
        AsyncFrameDecrypt* self = static_cast< AsyncFrameDecrypt* >(user);
        auto crypto             = self->crypto;
        SharedSecret K;
        SharedSecret sharedKey;
        // copy
        ProtocolFrame frame(self->frame);
        if(!crypto->pqe_decrypt(self->frame.C, K,
                                pq_keypair_to_secret(self->m_LocalIdentity.pq)))
        {
          LogError("pqke failed C=", self->frame.C);
          self->msg.reset();
          delete self;
          return;
        }
        // decrypt
        auto buf = frame.D.Buffer();
        crypto->xchacha20(*buf, K, self->frame.N);
        if(!self->msg->BDecode(buf))
        {
          LogError("failed to decode inner protocol message");
          DumpBuffer(*buf);
          self->msg.reset();
          delete self;
          return;
        }
        // verify signature of outer message after we parsed the inner message
        if(!self->frame.Verify(crypto, self->msg->sender))
        {
          LogError("intro frame has invalid signature Z=", self->frame.Z,
                   " from ", self->msg->sender.Addr());
          self->frame.Dump< MAX_PROTOCOL_MESSAGE_SIZE >();
          self->msg->Dump< MAX_PROTOCOL_MESSAGE_SIZE >();
          self->msg.reset();
          delete self;
          return;
        }

        if(self->handler->HasConvoTag(self->msg->tag))
        {
          LogError("dropping duplicate convo tag T=", self->msg->tag);
          // TODO: send convotag reset
          self->msg.reset();
          delete self;
          return;
        }

        // PKE (A, B, N)
        SharedSecret sharedSecret;
        using namespace std::placeholders;
        path_dh_func dh_server =
            std::bind(&Crypto::dh_server, self->crypto, _1, _2, _3, _4);

        if(!self->m_LocalIdentity.KeyExchange(dh_server, sharedSecret,
                                              self->msg->sender, self->frame.N))
        {
          LogError("x25519 key exchange failed");
          self->frame.Dump< MAX_PROTOCOL_MESSAGE_SIZE >();
          self->msg.reset();
          delete self;
          return;
        }
        std::array< byte_t, 64 > tmp;
        // K
        std::copy(K.begin(), K.end(), tmp.begin());
        // S = HS( K + PKE( A, B, N))
        std::copy(sharedSecret.begin(), sharedSecret.end(), tmp.begin() + 32);
        crypto->shorthash(sharedKey, llarp_buffer_t(tmp));

        self->handler->PutIntroFor(self->msg->tag, self->msg->introReply);
        self->handler->PutReplyIntroFor(self->msg->tag, self->fromIntro);
        self->handler->PutSenderFor(self->msg->tag, self->msg->sender);
        self->handler->PutCachedSessionKeyFor(self->msg->tag, sharedKey);

        self->msg->handler                     = self->handler;
        std::shared_ptr< ProtocolMessage > msg = std::move(self->msg);
        self->logic->queue_func([=]() { ProtocolMessage::ProcessAsync(msg); });
        delete self;
      }
    };

    ProtocolFrame&
    ProtocolFrame::operator=(const ProtocolFrame& other)
    {
      C       = other.C;
      D       = other.D;
      F       = other.F;
      N       = other.N;
      Z       = other.Z;
      T       = other.T;
      R       = other.R;
      S       = other.S;
      version = other.version;
      return *this;
    }

    bool
    ProtocolFrame::AsyncDecryptAndVerify(Logic* logic, Crypto* c,
                                         path::Path_ptr recvPath,
                                         llarp_threadpool* worker,
                                         const Identity& localIdent,
                                         IDataHandler* handler) const
    {
      auto msg = std::make_shared< ProtocolMessage >();
      if(T.IsZero())
      {
        LogInfo("Got protocol frame with new convo");
        msg->srcPath = recvPath->RXID();
        // we need to dh
        auto dh = new AsyncFrameDecrypt(logic, c, localIdent, handler, msg,
                                        *this, recvPath->intro);
        llarp_threadpool_queue_job(worker, {dh, &AsyncFrameDecrypt::Work});
        return true;
      }
      SharedSecret shared;
      if(!handler->GetCachedSessionKeyFor(T, shared))
      {
        LogError("No cached session for T=", T);
        return false;
      }
      ServiceInfo si;
      if(!handler->GetSenderFor(T, si))
      {
        LogError("No sender for T=", T);
        return false;
      }
      if(!Verify(c, si))
      {
        LogError("Signature failure from ", si.Addr());
        return false;
      }
      if(!DecryptPayloadInto(c, shared, *msg))
      {
        LogError("failed to decrypt message");
        return false;
      }
      msg->srcPath = recvPath->RXID();
      msg->handler = handler;
      logic->queue_func([=]() { ProtocolMessage::ProcessAsync(msg); });
      return true;
    }

    bool
    ProtocolFrame::operator==(const ProtocolFrame& other) const
    {
      return C == other.C && D == other.D && N == other.N && Z == other.Z
          && T == other.T && S == other.S && version == other.version;
    }

    bool
    ProtocolFrame::Verify(Crypto* crypto, const ServiceInfo& from) const
    {
      ProtocolFrame copy(*this);
      // save signature
      // zero out signature for verify
      copy.Z.Zero();
      // serialize
      std::array< byte_t, MAX_PROTOCOL_MESSAGE_SIZE > tmp;
      llarp_buffer_t buf(tmp);
      if(!copy.BEncode(&buf))
      {
        LogError("bencode fail");
        return false;
      }

      // rewind buffer
      buf.sz  = buf.cur - buf.base;
      buf.cur = buf.base;
      // verify
      return from.Verify(crypto, buf, Z);
    }

    bool
    ProtocolFrame::HandleMessage(routing::IMessageHandler* h,
                                 ABSL_ATTRIBUTE_UNUSED AbstractRouter* r) const
    {
      return h->HandleHiddenServiceFrame(*this);
    }

  }  // namespace service
}  // namespace llarp
