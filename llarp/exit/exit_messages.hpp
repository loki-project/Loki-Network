#ifndef LLARP_EXIT_MESSAGES_HPP
#define LLARP_EXIT_MESSAGES_HPP

#include <crypto/types.hpp>
#include <exit/policy.hpp>
#include <routing/message.hpp>

#include <vector>

namespace llarp
{
  namespace routing
  {
    struct ObtainExitMessage final : public IMessage
    {
      std::vector< llarp::exit::Policy > B;
      uint64_t E;
      llarp::PubKey I;
      uint64_t T;
      std::vector< llarp::exit::Policy > W;
      llarp_time_t X;
      llarp::Signature Z;

      ObtainExitMessage() : IMessage(), E(0), T(0), X(0)
      {
      }

      ~ObtainExitMessage()
      {
      }

      void
      Clear() override
      {
        B.clear();
        E = 0;
        I.Zero();
        T = 0;
        W.clear();
        X = 0;
        Z.Zero();
      }

      /// populates I and signs
      bool
      Sign(const llarp::SecretKey& sk);

      bool
      Verify() const;

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;
    };

    struct GrantExitMessage final : public IMessage
    {
      using Nonce_t = llarp::AlignedBuffer< 16 >;

      uint64_t T;
      Nonce_t Y;
      llarp::Signature Z;

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      Sign(const llarp::SecretKey& sk);

      bool
      Verify(const llarp::PubKey& pk) const;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;

      void
      Clear() override
      {
        T = 0;
        Y.Zero();
        Z.Zero();
      }
    };

    struct RejectExitMessage final : public IMessage
    {
      using Nonce_t = llarp::AlignedBuffer< 16 >;
      uint64_t B;
      std::vector< llarp::exit::Policy > R;
      uint64_t T;
      Nonce_t Y;
      llarp::Signature Z;

      void
      Clear() override
      {
        B = 0;
        R.clear();
        T = 0;
        Y.Zero();
        Z.Zero();
      }

      bool
      Sign(const llarp::SecretKey& sk);

      bool
      Verify(const llarp::PubKey& pk) const;

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;
    };

    struct UpdateExitVerifyMessage final : public IMessage
    {
      using Nonce_t = llarp::AlignedBuffer< 16 >;
      uint64_t T;
      Nonce_t Y;
      llarp::Signature Z;

      ~UpdateExitVerifyMessage() = default;

      void
      Clear() override
      {
        T = 0;
        Y.Zero();
        Z.Zero();
      }

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;
    };

    struct UpdateExitMessage final : public IMessage
    {
      using Nonce_t = llarp::AlignedBuffer< 16 >;
      llarp::PathID_t P;
      uint64_t T;
      Nonce_t Y;
      llarp::Signature Z;

      bool
      Sign(const llarp::SecretKey& sk);

      bool
      Verify(const llarp::PubKey& pk) const;

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;

      void
      Clear() override
      {
        P.Zero();
        T = 0;
        Y.Zero();
        Z.Zero();
      }
    };

    struct CloseExitMessage final : public IMessage
    {
      using Nonce_t = llarp::AlignedBuffer< 16 >;

      Nonce_t Y;
      llarp::Signature Z;

      bool
      BEncode(llarp_buffer_t* buf) const override;

      bool
      DecodeKey(const llarp_buffer_t& key, llarp_buffer_t* buf) override;

      bool
      HandleMessage(IMessageHandler* h, AbstractRouter* r) const override;

      bool
      Sign(const llarp::SecretKey& sk);

      bool
      Verify(const llarp::PubKey& pk) const;

      void
      Clear() override
      {
        Y.Zero();
        Z.Zero();
      }
    };

  }  // namespace routing
}  // namespace llarp

#endif
