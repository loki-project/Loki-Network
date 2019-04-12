#include <config.hpp>  // for ensure_config
#include <libgen.h>
#include <llarp.h>
#include <util/fs.hpp>
#include <util/logger.hpp>

#include <getopt.h>
#include <signal.h>

#if !defined(_WIN32) && !defined(__OpenBSD__)
#include <wordexp.h>
#endif

#include <string>
#include <iostream>

#ifdef _WIN32
#define wmin(x, y) (((x) < (y)) ? (x) : (y))
#define MIN wmin
#endif

struct llarp_main *ctx = 0;

void
handle_signal(int sig)
{
  if(ctx)
    llarp_main_signal(ctx, sig);
}

int
printHelp(const char *argv0, int code = 1)
{
  std::cout << "usage: " << argv0 << " [-h] [-v] [-g|-c] config.ini"
            << std::endl;
  return code;
}

#ifdef _WIN32
int
startWinsock()
{
  WSADATA wsockd;
  int err;
  err = ::WSAStartup(MAKEWORD(2, 2), &wsockd);
  if(err)
  {
    perror("Failed to start Windows Sockets");
    return err;
  }
  return 0;
}

extern "C" BOOL FAR PASCAL
handle_signal_win32(DWORD fdwCtrlType)
{
  UNREFERENCED_PARAMETER(fdwCtrlType);
  handle_signal(SIGINT);
  return TRUE;  // probably unreachable
}
#endif

/// resolve ~ and symlinks into actual paths (so we know the real path on disk,
/// to remove assumptions and confusion with permissions)
std::string
resolvePath(std::string conffname)
{
  // implemented in netbsd, removed downstream for security reasons
  // even though it is defined by POSIX.1-2001+
#if !defined(_WIN32) && !defined(__OpenBSD__)
  wordexp_t exp_result;
  wordexp(conffname.c_str(), &exp_result, 0);
  char *resolvedPath = realpath(exp_result.we_wordv[0], NULL);
  if(!resolvedPath)
  {
    llarp::LogWarn("Can't resolve path: ", exp_result.we_wordv[0]);
    return "";
  }
  return resolvedPath;
#else
  // TODO(despair): dig through LLVM local patch set
  // one of these exists deep in the bowels of LLVMSupport
  return conffname;  // eww, easier said than done outside of cygwin
#endif
}

int
main(int argc, char *argv[])
{
  bool multiThreaded          = true;
  const char *singleThreadVar = getenv("LLARP_SHADOW");
  if(singleThreadVar && std::string(singleThreadVar) == "1")
  {
    multiThreaded = false;
  }

#ifdef _WIN32
  if(startWinsock())
    return -1;
  SetConsoleCtrlHandler(handle_signal_win32, TRUE);
#endif

#ifdef LOKINET_DEBUG
  absl::SetMutexDeadlockDetectionMode(absl::OnDeadlockCycle::kAbort);
#endif

  int opt            = 0;
  bool genconfigOnly = false;
  bool asRouter      = false;
  bool overWrite     = false;
  while((opt = getopt(argc, argv, "hgcfrv")) != -1)
  {
    switch(opt)
    {
      case 'v':
        SetLogLevel(llarp::eLogDebug);
        llarp::LogDebug("debug logging activated");
        break;
      case 'h':
        return printHelp(argv[0], 0);
      case 'g':
        genconfigOnly = true;
        break;
      case 'c':
        genconfigOnly = true;
        break;
      case 'r':
        asRouter = true;
        break;
      case 'f':
        overWrite = true;
        break;
      default:
        return printHelp(argv[0]);
    }
  }

  std::string conffname;  // suggestions: confFName? conf_fname?

  if(optind < argc)
  {
    // when we have an explicit filepath
    fs::path fname   = fs::path(argv[optind]);
    fs::path basedir = fname.parent_path();
    conffname        = fname.string();
    conffname        = resolvePath(conffname);
    std::error_code ec;

    // llarp::LogDebug("Basedir: ", basedir);
    if(basedir.string().empty())
    {
      // relative path to config

      // does this file exist?
      if(genconfigOnly)
      {
        if(!llarp_ensure_config(conffname.c_str(), nullptr, overWrite,
                                asRouter))
          return 1;
      }
      else
      {
        if(!fs::exists(fname, ec))
        {
          llarp::LogError("Config file not found ", conffname);
          return 1;
        }
      }
    }
    else
    {
      // absolute path to config
      if(!fs::create_directories(basedir, ec))
      {
        if(ec)
        {
          llarp::LogError("failed to create '", basedir.string(),
                          "': ", ec.message());
          return 1;
        }
      }
      if(genconfigOnly)
      {
        // find or create file
        if(!llarp_ensure_config(conffname.c_str(), basedir.string().c_str(),
                                overWrite, asRouter))
          return 1;
      }
      else
      {
        // does this file exist?
        if(!fs::exists(conffname, ec))
        {
          llarp::LogError("Config file not found ", conffname);
          return 1;
        }
      }
    }
  }
  else
  {
// no explicit config file provided
#ifdef _WIN32
    fs::path homedir = fs::path(getenv("APPDATA"));
#else
    fs::path homedir = fs::path(getenv("HOME"));
#endif
    fs::path basepath = homedir / fs::path(".lokinet");
    fs::path fpath    = basepath / "lokinet.ini";
    // I don't think this is necessary with this condition
    // conffname = resolvePath(conffname);

    llarp::LogDebug("Find or create ", basepath.string());
    std::error_code ec;
    // These paths are guaranteed to exist - $APPDATA or $HOME
    // so only create .lokinet/*
    if(!fs::create_directory(basepath, ec))
    {
      if(ec)
      {
        llarp::LogError("failed to create '", basepath.string(),
                        "': ", ec.message());
        return 1;
      }
    }

    // if using default INI file, we're create it even if you don't ask us too
    if(!llarp_ensure_config(fpath.string().c_str(), basepath.string().c_str(),
                            overWrite, asRouter))
      return 1;
    conffname = fpath.string();
  }

  if(genconfigOnly)
  {
    return 0;
  }

  // this is important, can downgrade from Info though
  llarp::LogInfo("Running from: ", fs::current_path().string());
  llarp::LogInfo("Using config file: ", conffname);
  ctx      = llarp_main_init(conffname.c_str(), multiThreaded);
  int code = 1;
  if(ctx)
  {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#ifndef _WIN32
    signal(SIGHUP, handle_signal);
#endif

    code = llarp_main_setup(ctx);
    if(code == 0)
      code = llarp_main_run(ctx);
    llarp_main_free(ctx);
  }
#ifdef _WIN32
  ::WSACleanup();
#endif
  exit(code);
  return code;
}
