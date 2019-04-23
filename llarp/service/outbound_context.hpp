#ifndef LLARP_SERVICE_OUTBOUND_CONTEXT_HPP
#define LLARP_SERVICE_OUTBOUND_CONTEXT_HPP

#include <path/pathbuilder.hpp>
#include <service/sendcontext.hpp>
#include <util/status.hpp>

#include <unordered_map>

namespace llarp
{
  namespace service
  {
    struct AsyncKeyExchange;
    struct Endpoint;

    /// context needed to initiate an outbound hidden service session
    struct OutboundContext : public path::Builder, public SendContext
    {
      OutboundContext(const IntroSet& introSet, Endpoint* parent);
      ~OutboundContext();

      util::StatusObject
      ExtractStatus() const;

      bool
      ShouldBundleRC() const override;

      bool
      Stop() override;

      bool
      HandleDataDrop(path::Path* p, const PathID_t& dst, uint64_t s);

      void
      HandlePathDied(path::Path* p) override;

      /// set to true if we are updating the remote introset right now
      bool updatingIntroSet;

      /// update the current selected intro to be a new best introduction
      /// return true if we have changed intros
      bool
      ShiftIntroduction(bool rebuild = true) override;

      /// mark the current remote intro as bad
      bool
      MarkCurrentIntroBad(llarp_time_t now) override;

      /// return true if we are ready to send
      bool
      ReadyToSend() const;

      bool
      ShouldBuildMore(llarp_time_t now) const override;

      /// tick internal state
      /// return true to mark as dead
      bool
      Tick(llarp_time_t now);

      /// return true if it's safe to remove ourselves
      bool
      IsDone(llarp_time_t now) const;

      bool
      CheckPathIsDead(path::Path* p, llarp_time_t dlt);

      void
      AsyncGenIntro(const llarp_buffer_t& payload, ProtocolType t) override;

      /// issues a lookup to find the current intro set of the remote service
      void
      UpdateIntroSet(bool randomizePath) override;

      bool
      BuildOneAlignedTo(const RouterID& remote);

      void
      HandlePathBuilt(path::Path* path) override;

      bool
      SelectHop(llarp_nodedb* db, const RouterContact& prev, RouterContact& cur,
                size_t hop, path::PathRole roles) override;

      bool
      HandleHiddenServiceFrame(path::Path* p, const ProtocolFrame& frame);

      std::string
      Name() const override;

     private:
      /// swap remoteIntro with next intro
      void
      SwapIntros();

      void
      OnGeneratedIntroFrame(AsyncKeyExchange* k, PathID_t p);

      bool
      OnIntroSetUpdate(const Address& addr, const IntroSet* i,
                       const RouterID& endpoint);

      uint64_t m_UpdateIntrosetTX = 0;
      IntroSet currentIntroSet;
      Introduction m_NextIntro;
      std::unordered_map< Introduction, llarp_time_t, Introduction::Hash >
          m_BadIntros;
      llarp_time_t lastShift = 0;
      uint16_t m_LookupFails = 0;
      uint16_t m_BuildFails  = 0;
    };
  }  // namespace service

}  // namespace llarp
#endif
