#include <dns/srv_data.hpp>
#include <util/str.hpp>

#include <limits>

namespace llarp::dns
{

  bool SRVData::IsValid() const
  {
    // if target is of first two forms outlined above
    if (target == "." or target.size() == 0)
    {
      return true;
    }

    // check target size is not absurd
    if (target.size() > TARGET_MAX_SIZE)
    {
      return false;
    }

    // does target end in .loki?
    size_t pos = target.find(".loki");
    if (pos != std::string::npos && pos == (target.size() - 5))
    {
      return true;
    }

    // does target end in .snode?
    pos = target.find(".snode");
    if (pos != std::string::npos && pos == (target.size() - 6))
    {
      return true;
    }

    // if we're here, target is invalid
    return false;
  }

  SRVTuple SRVData::toTuple() const
  {
    return std::make_tuple(service_proto, priority, weight, port, target);
  }

  SRVData SRVData::fromTuple(SRVTuple tuple)
  {
    SRVData s;

    std::tie(s.service_proto, s.priority, s.weight, s.port, s.target) = std::move(tuple);

    return s;
  }

  bool SRVData::fromString(std::string_view srvString)
  {
    // split on spaces, discard trailing empty strings
    auto splits = split(srvString, " ", false);

    if (splits.size() != 5 && splits.size() != 4)
    {
      return false;
    }

    service_proto = splits[0];

    if (not parse_int(splits[1], priority))
      return false;

    if (not parse_int(splits[2], weight))
      return false;

    if (not parse_int(splits[3], port))
      return false;

    if (splits.size() == 5)
      target = splits[4];
    else
      target = "";

    return IsValid();
  }

} // namespace llarp::dns
