#ifndef LLARP_PATHBUILDER_HPP_
#define LLARP_PATHBUILDER_HPP_

#include <path/pathset.hpp>
#include <util/status.hpp>

#include <atomic>

struct llarp_dht_context;

namespace llarp
{
  namespace path
  {
    // milliseconds waiting between builds on a path
    constexpr llarp_time_t MIN_PATH_BUILD_INTERVAL = 500;

    struct Builder : public PathSet
    {
     protected:
      /// flag for PathSet::Stop()
      std::atomic< bool > _run;

      virtual bool
      UrgentBuild(llarp_time_t now) const;

     public:
      AbstractRouter* router;
      llarp_dht_context* dht;
      SecretKey enckey;
      size_t numHops;
      llarp_time_t lastBuild          = 0;
      llarp_time_t buildIntervalLimit = MIN_PATH_BUILD_INTERVAL;

      /// construct
      Builder(AbstractRouter* p_router, llarp_dht_context* p_dht,
              size_t numPaths, size_t numHops);

      virtual ~Builder();

      util::StatusObject
      ExtractStatus() const;

      virtual bool
      SelectHop(llarp_nodedb* db, const std::set< RouterID >& prev,
                RouterContact& cur, size_t hop, PathRole roles) override;

      virtual bool
      ShouldBuildMore(llarp_time_t now) const override;

      /// should we bundle RCs in builds?
      virtual bool
      ShouldBundleRC() const = 0;

      virtual void
      ResetInternalState() override;

      /// return true if we hit our soft limit for building paths too fast
      bool
      BuildCooldownHit(llarp_time_t now) const;

      /// get roles for this path builder
      virtual PathRole
      GetRoles() const
      {
        return ePathRoleAny;
      }

      virtual bool
      Stop() override;

      bool
      IsStopped() const override;

      bool
      ShouldRemove() const override;

      llarp_time_t
      Now() const override;

      virtual void
      Tick(llarp_time_t now) override;

      void
      BuildOne(PathRole roles = ePathRoleAny) override;

      bool
      BuildOneAlignedTo(const RouterID endpoint) override;

      void
      Build(const std::vector< RouterContact >& hops,
            PathRole roles = ePathRoleAny) override;

      bool
      SelectHops(llarp_nodedb* db, std::vector< RouterContact >& hops,
                 PathRole roles = ePathRoleAny);

      void
      ManualRebuild(size_t N, PathRole roles = ePathRoleAny);

      virtual const SecretKey&
      GetTunnelEncryptionSecretKey() const;

      virtual void
      HandlePathBuilt(Path_ptr p) override;

      virtual void
      HandlePathBuildTimeout(Path_ptr p) override;
    };

    using Builder_ptr = std::shared_ptr< Builder >;

  }  // namespace path

}  // namespace llarp
#endif
