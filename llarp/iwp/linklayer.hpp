#ifndef LLARP_IWP_LINKLAYER_HPP
#define LLARP_IWP_LINKLAYER_HPP

#include <constants/link_layer.hpp>
#include <crypto/crypto.hpp>
#include <crypto/encrypted.hpp>
#include <crypto/types.hpp>
#include <link/server.hpp>

namespace llarp
{
  namespace iwp
  {
    struct LinkLayer final : public ILinkLayer
    {
      LinkLayer(const SecretKey &routerEncSecret, GetRCFunc getrc,
                LinkMessageHandler h, SignBufferFunc sign,
                SessionEstablishedHandler est, SessionRenegotiateHandler reneg,
                TimeoutHandler timeout, SessionClosedHandler closed,
                bool permitInbound);

      ~LinkLayer() override;

      bool
      Start(std::shared_ptr< Logic > l) override;

      std::shared_ptr< ILinkSession >
      NewOutboundSession(const RouterContact &rc,
                         const AddressInfo &ai) override;

      void
      Pump() override LOCKS_EXCLUDED(m_AuthedLinksMutex);

      bool
      KeyGen(SecretKey &k) override;

      const char *
      Name() const override;

      uint16_t
      Rank() const override;

      void
      RecvFrom(const Addr &from, const void *buf, size_t sz) override
          LOCKS_EXCLUDED(m_PendingMutex, m_AuthedLinksMutex);

      bool
      MapAddr(const RouterID &pk, ILinkSession *s) override
          LOCKS_EXCLUDED(m_AuthedLinksMutex);

      void
      UnmapAddr(const Addr &addr) LOCKS_EXCLUDED(m_AuthedLinksMutex);

     private:
      std::unordered_map< Addr, RouterID, Addr::Hash > m_AuthedAddrs
          GUARDED_BY(m_AuthedLinksMutex);
      const bool permitInbound;
    };

    using LinkLayer_ptr = std::shared_ptr< LinkLayer >;
  }  // namespace iwp
}  // namespace llarp

#endif
