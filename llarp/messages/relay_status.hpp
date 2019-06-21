#ifndef LLARP_RELAY_STATUS_HPP
#define LLARP_RELAY_STATUS_HPP

#include <crypto/encrypted_frame.hpp>
#include <crypto/types.hpp>
#include <messages/link_message.hpp>
#include <path/path_types.hpp>
#include <pow.hpp>

#include <array>
#include <memory>

namespace llarp
{
  // forward declare
  struct AbstractRouter;
  namespace path
  {
    struct PathContext;
    struct IHopHandler;
  }

  struct LR_StatusRecord
  {
    static constexpr uint64_t SUCCESS = 1;
    static constexpr uint64_t FAIL_TIMEOUT = 1 << 1;
    static constexpr uint64_t FAIL_CONGESTION = 1 << 2;
    static constexpr uint64_t FAIL_DEST_UNKNOWN = 1 << 3;
    static constexpr uint64_t FAIL_DECRYPT_ERROR = 1 << 4;
    static constexpr uint64_t FAIL_MALFORMED_RECORD = 1 << 5;

    uint64_t status = 0;
    uint64_t version  = 0;

    bool
    BDecode(llarp_buffer_t *buf);

    bool
    BEncode(llarp_buffer_t *buf) const;

    bool
    operator==(const LR_StatusRecord &other) const;

   private:
    bool
    OnKey(llarp_buffer_t *buffer, llarp_buffer_t *key);
  };

  struct LR_StatusMessage : public ILinkMessage
  {
    std::array< EncryptedFrame, 8 > frames;

    PathID_t pathid;

    uint64_t status = 0;

    LR_StatusMessage(const std::array< EncryptedFrame, 8 > &_frames)
        : ILinkMessage(), frames(_frames)
    {
    }

    LR_StatusMessage() = default;

    ~LR_StatusMessage() = default;

    void
    Clear() override;

    bool
    DecodeKey(const llarp_buffer_t &key, llarp_buffer_t *buf) override;

    bool
    BEncode(llarp_buffer_t *buf) const override;

    bool
    HandleMessage(AbstractRouter *router) const override;

    void
    SetDummyFrames();

    static bool
    CreateAndSend(AbstractRouter *router, const PathID_t pathid,
                  const RouterID nextHop, const SharedSecret pathKey,
                  uint64_t status);

    bool
    AddFrame(const SharedSecret& pathKey, uint64_t status);

    static void
    QueueSendMessage(AbstractRouter *router, const RouterID nextHop,
                     std::shared_ptr< LR_StatusMessage > msg);

    static void
    SendMessage(AbstractRouter *router, const RouterID nextHop,
                std::shared_ptr< LR_StatusMessage > msg);

    const char *
    Name() const override
    {
      return "RelayStatus";
    }
  };
}  // namespace llarp

#endif
