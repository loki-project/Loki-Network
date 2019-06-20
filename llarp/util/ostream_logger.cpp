#include <util/ostream_logger.hpp>
#include <util/logger_internal.hpp>

namespace llarp
{
  OStreamLogStream::OStreamLogStream(std::ostream& out) : m_Out(out)
  {
  }

  void
  OStreamLogStream::PreLog(std::stringstream& ss, LogLevel lvl,
                           const char* fname, int lineno,
                           const std::string& nodename) const
  {
    switch(lvl)
    {
      case eLogNone:
        break;
      case eLogDebug:
        ss << (char)27 << "[0m";
        break;
      case eLogInfo:
        ss << (char)27 << "[1m";
        break;
      case eLogWarn:
        ss << (char)27 << "[1;33m";
        break;
      case eLogError:
        ss << (char)27 << "[1;31m";
        break;
    }
    ss << "[" << LogLevelToString(lvl) << "] ";
    ss << "[" << nodename << "]"
       << "(" << thread_id_string() << ") " << log_timestamp() << " " << fname
       << ":" << lineno << "\t";
  }

  void
  OStreamLogStream::PostLog(std::stringstream& ss) const
  {
    ss << (char)27 << "[0;0m" << std::endl;
  }

  void
  OStreamLogStream::Print(LogLevel /*lvl*/, const char* /*filename*/, const std::string& msg)
  {
    m_Out << msg << std::flush;
  }

}  // namespace llarp
