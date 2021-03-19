#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>

#include "address.hpp"

namespace llarp::service
{
  /// tracks and manages consensus of lns names we fetch from the network
  class LNSLookupTracker
  {
    struct LookupInfo
    {
      std::unordered_set<Address> m_CurrentValues;
      std::function<void(std::optional<Address>)> m_HandleResult;
      std::size_t m_ResultsGotten = 0;
      std::size_t m_ResultsNeeded;

      LookupInfo(std::size_t wantResults, std::function<void(std::optional<Address>)> resultHandler)
          : m_HandleResult{std::move(resultHandler)}, m_ResultsNeeded{wantResults}
      {}

      bool
      IsDone() const;

      void
      HandleOneResult(std::optional<Address> result);
    };

    std::unordered_map<std::string, LookupInfo> m_PendingLookups;

   public:
    /// make a function that will handle consensus of an lns request
    /// name is the name we are requesting
    /// numPeers is the number of peers we asked
    /// resultHandler is a function that we are wrapping that will handle the final result
    std::function<void(std::optional<Address>)>
    MakeResultHandler(
        std::string name,
        std::size_t numPeers,
        std::function<void(std::optional<Address>)> resultHandler);
  };
}  // namespace llarp::service