#ifndef LLARP_HPP
#define LLARP_HPP
#include <llarp.h>

#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llarp
{
  namespace vpn
  {
    class Platform;
  }

  class Logic;
  struct Config;
  struct RouterContact;
  struct Config;
  struct Crypto;
  struct CryptoManager;
  struct AbstractRouter;
  struct EventLoop;
  class NodeDB;
  
  namespace thread
  {
    class ThreadPool;
  }

  struct RuntimeOptions
  {
    bool background = false;
    bool debug = false;
    bool isRouter = false;
  };

  struct Context
  {
    std::shared_ptr<Crypto> crypto = nullptr;
    std::shared_ptr<CryptoManager> cryptoManager = nullptr;
    std::shared_ptr<AbstractRouter> router = nullptr;
    std::shared_ptr<Logic> logic = nullptr;
    std::shared_ptr<NodeDB> nodedb = nullptr;
    std::shared_ptr<EventLoop> mainloop;
    fs::path nodedb_dir;

    virtual ~Context() = default;

    void
    Close();

    void
    Setup(const RuntimeOptions& opts);

    int
    Run(const RuntimeOptions& opts);

    void
    HandleSignal(int sig);

    /// Configure given the specified config.
    void
    Configure(std::shared_ptr<Config> conf);

    /// handle SIGHUP
    void
    Reload();

    bool
    IsUp() const;

    bool
    LooksAlive() const;

    /// close async
    void
    CloseAsync();

    /// wait until closed and done
    void
    Wait();

    /// call a function in logic thread
    /// return true if queued for calling
    /// return false if not queued for calling
    bool
    CallSafe(std::function<void(void)> f);

    /// Creates a router. Can be overridden to allow a different class of router
    /// to be created instead. Defaults to llarp::Router.
    virtual std::shared_ptr<AbstractRouter>
    makeRouter(std::shared_ptr<EventLoop> __netloop, std::shared_ptr<Logic> logic);

    /// create the vpn platform for use in creating network interfaces
    virtual std::shared_ptr<llarp::vpn::Platform>
    makeVPNPlatform();

   protected:
    std::shared_ptr<Config> config = nullptr;

   private:
    void
    SigINT();

    std::unique_ptr<std::promise<void>> closeWaiter;
  };

}  // namespace llarp

#endif
