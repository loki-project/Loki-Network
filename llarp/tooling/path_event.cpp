#include <tooling/path_event.hpp>

#include <path/path.hpp>
#include <path/transit_hop.hpp>

namespace tooling
{
  PathAttemptEvent::PathAttemptEvent(
      const llarp::RouterID& routerID,
      std::shared_ptr< const llarp::path::Path > path)
      : RouterEvent("PathAttemptEvent", routerID, false)
      , hops(path->hops)
      , pathid(path->hops[0].rxID)
  {
  }

  std::string
  PathAttemptEvent::ToString() const
  {
    std::string result = RouterEvent::ToString();
    result += "---- [";

    size_t i = 0;
    for(const auto& hop : hops)
    {
      i++;

      result += llarp::RouterID(hop.rc.pubkey).ShortString();
      result += "]";

      if(i != hops.size())
      {
        result += " -> [";
      }
    }

    return result;
  }

  PathRequestReceivedEvent::PathRequestReceivedEvent(
      const llarp::RouterID& routerID,
      std::shared_ptr< const llarp::path::TransitHop > hop)
      : RouterEvent("PathRequestReceivedEvent", routerID, true)
      , prevHop(hop->info.downstream)
      , nextHop(hop->info.upstream)
      , txid(hop->info.txID)
      , rxid(hop->info.rxID)
  {
    isEndpoint = false;
    if(routerID == nextHop)
    {
      isEndpoint = true;
    }
  }

  std::string
  PathRequestReceivedEvent::ToString() const
  {
    std::string result = RouterEvent::ToString();
    result += "---- [";
    result += prevHop.ShortString();
    result += "] -> [*";
    result += routerID.ShortString();
    result += "] -> [";

    if(isEndpoint)
    {
      result += "nowhere]";
    }
    else
    {
      result += nextHop.ShortString();
      result += "]";
    }

    return result;
  }

  PathStatusReceivedEvent::PathStatusReceivedEvent(
      const llarp::RouterID& routerID, const llarp::PathID_t rxid,
      uint64_t status)
      : RouterEvent("PathStatusReceivedEvent", routerID, true)
      , rxid(rxid)
      , status(status)
  {
  }

  std::string
  PathStatusReceivedEvent::ToString() const
  {
    std::string result = RouterEvent::ToString();
    result += "---- path rxid: " + rxid.ShortHex();
    result += ", status: " + std::to_string(status);

    return result;
  }

  PathBuildRejectedEvent::PathBuildRejectedEvent(
      const llarp::RouterID& routerID,
      const llarp::PathID_t rxid_,
      const llarp::RouterID& rejectedBy_)
      : RouterEvent("PathBuildRejectedEvent", routerID, false)
      , rxid(rxid_)
      , rejectedBy(rejectedBy_)
  {
  }

  std::string
  PathBuildRejectedEvent::ToString() const
  {
    std::string result = RouterEvent::ToString();
    result += "---- path rxid: " + rxid.ShortHex();
    result += ", rejectedBy: " + rejectedBy.ShortString();

    return result;
  }

}  // namespace tooling
