#include <peerstats/peer_db.hpp>
#include <test_util.hpp>

#include <numeric>
#include <catch2/catch.hpp>
#include "peerstats/types.hpp"
#include "router_contact.hpp"
#include "util/time.hpp"

TEST_CASE("Test PeerDb PeerStats memory storage", "[PeerDb]")
{
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0x01);
  const llarp::PeerStats empty(id);

  llarp::PeerDb db;
  CHECK(db.getCurrentPeerStats(id).has_value() == false);

  llarp::PeerStats delta(id);
  delta.numConnectionAttempts = 4;
  delta.peakBandwidthBytesPerSec = 5;
  db.accumulatePeerStats(id, delta);
  CHECK(db.getCurrentPeerStats(id).value() == delta);

  delta = llarp::PeerStats(id);
  delta.numConnectionAttempts = 5;
  delta.peakBandwidthBytesPerSec = 6;
  db.accumulatePeerStats(id, delta);

  llarp::PeerStats expected(id);
  expected.numConnectionAttempts = 9;
  expected.peakBandwidthBytesPerSec = 6;
  CHECK(db.getCurrentPeerStats(id).value() == expected);
}

TEST_CASE("Test PeerDb flush before load", "[PeerDb]")
{
  llarp::PeerDb db;
  CHECK_THROWS_WITH(db.flushDatabase(), "Cannot flush database before it has been loaded");
}

TEST_CASE("Test PeerDb load twice", "[PeerDb]")
{
  llarp::PeerDb db;
  CHECK_NOTHROW(db.loadDatabase(std::nullopt));
  CHECK_THROWS_WITH(db.loadDatabase(std::nullopt), "Reloading database not supported");
}

TEST_CASE("Test PeerDb nukes stats on load", "[PeerDb]")
{
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0x01);

  llarp::PeerDb db;

  llarp::PeerStats stats(id);
  stats.numConnectionAttempts = 1;

  db.accumulatePeerStats(id, stats);
  CHECK(db.getCurrentPeerStats(id).value() == stats);

  db.loadDatabase(std::nullopt);

  CHECK(db.getCurrentPeerStats(id).has_value() == false);
}

TEST_CASE("Test PeerDb file-backed database reloads properly", "[PeerDb]")
{
  const std::string filename = "/tmp/peerdb_test_tmp2.db.sqlite";
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0x02);

  {
    llarp::PeerDb db;
    db.loadDatabase(filename);

    llarp::PeerStats stats(id);
    stats.numConnectionAttempts = 43;

    db.accumulatePeerStats(id, stats);

    db.flushDatabase();
  }

  {
    llarp::PeerDb db;
    db.loadDatabase(filename);

    auto stats = db.getCurrentPeerStats(id);
    CHECK(stats.has_value() == true);
    CHECK(stats.value().numConnectionAttempts == 43);
  }

  fs::remove(filename);
}

TEST_CASE("Test PeerDb modifyPeerStats", "[PeerDb]")
{
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0xF2);

  int numTimesCalled = 0;

  llarp::PeerDb db;
  db.loadDatabase(std::nullopt);

  db.modifyPeerStats(id, [&](llarp::PeerStats& stats) {
    numTimesCalled++;

    stats.numPathBuilds += 42;
  });

  db.flushDatabase();

  CHECK(numTimesCalled == 1);

  auto stats = db.getCurrentPeerStats(id);
  CHECK(stats.has_value());
  CHECK(stats.value().numPathBuilds == 42);
}

TEST_CASE("Test PeerDb handleGossipedRC", "[PeerDb]")
{
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0xCA);

  auto rcLifetime = llarp::RouterContact::Lifetime;
  llarp_time_t now = 0s;

  llarp::RouterContact rc;
  rc.pubkey = llarp::PubKey(id);
  rc.last_updated = 10s;

  llarp::PeerDb db;
  db.handleGossipedRC(rc, now);

  auto stats = db.getCurrentPeerStats(id);
  CHECK(stats.has_value());
  CHECK(stats.value().mostExpiredRCMs == 0);  // not calculated on first received RC
  CHECK(stats.value().numDistinctRCsReceived == 1);
  CHECK(stats.value().lastRCUpdated == 10000);

  now = 9s;
  db.handleGossipedRC(rc, now);
  stats = db.getCurrentPeerStats(id);
  CHECK(stats.has_value());
  // these values should remain unchanged, this is not a new RC
  CHECK(stats.value().mostExpiredRCMs == 0);
  CHECK(stats.value().numDistinctRCsReceived == 1);
  CHECK(stats.value().lastRCUpdated == 10000);

  rc.last_updated = 11s;

  db.handleGossipedRC(rc, now);
  stats = db.getCurrentPeerStats(id);
  // should be (previous expiration time - new received time)
  CHECK(stats.value().mostExpiredRCMs == ((10s + rcLifetime) - now).count());
  CHECK(stats.value().numDistinctRCsReceived == 2);
  CHECK(stats.value().lastRCUpdated == 11000);
}

TEST_CASE("Test PeerDb handleGossipedRC expiry calcs", "[PeerDb]")
{
  const llarp::RouterID id = llarp::test::makeBuf<llarp::RouterID>(0xF9);

  // see comments in peer_db.cpp above PeerDb::handleGossipedRC() for some context around these
  // tests and esp. these numbers
  const llarp_time_t ref = 48h;
  const llarp_time_t rcLifetime = llarp::RouterContact::Lifetime;

  // rc1, first rc received
  const llarp_time_t s1 = ref;
  const llarp_time_t r1 = s1 + 30s;
  const llarp_time_t e1 = s1 + rcLifetime;
  llarp::RouterContact rc1;
  rc1.pubkey = llarp::PubKey(id);
  rc1.last_updated = s1;

  // rc2, second rc received
  // received "healthily", with lots of room to spare before rc1 expires
  const llarp_time_t s2 = s1 + 8h;
  const llarp_time_t r2 = s2 + 30s;  // healthy recv time
  const llarp_time_t e2 = s2 + rcLifetime;
  llarp::RouterContact rc2;
  rc2.pubkey = llarp::PubKey(id);
  rc2.last_updated = s2;

  // rc3, third rc received
  // received "unhealthily" (after rc2 expires)
  const llarp_time_t s3 = s2 + 8h;
  const llarp_time_t r3 = e2 + 1h;  // received after e2
  const llarp_time_t e3 = s3 + rcLifetime;
  llarp::RouterContact rc3;
  rc3.pubkey = llarp::PubKey(id);
  rc3.last_updated = s3;

  llarp::PeerDb db;

  db.handleGossipedRC(rc1, r1);
  auto stats1 = db.getCurrentPeerStats(id);
  CHECK(stats1.has_value());
  CHECK(stats1.value().mostExpiredRCMs == 0);
  CHECK(stats1.value().numDistinctRCsReceived == 1);
  CHECK(stats1.value().lastRCUpdated == s1.count());

  db.handleGossipedRC(rc2, r2);
  auto stats2 = db.getCurrentPeerStats(id);
  CHECK(stats2.has_value());
  CHECK(stats2.value().mostExpiredRCMs == (e1 - r2).count());
  CHECK(stats2.value().mostExpiredRCMs > 0);  // ensure positive indicates healthy
  CHECK(stats2.value().numDistinctRCsReceived == 2);
  CHECK(stats2.value().lastRCUpdated == s2.count());

  db.handleGossipedRC(rc3, r3);
  auto stats3 = db.getCurrentPeerStats(id);
  CHECK(stats3.has_value());
  CHECK(stats3.value().mostExpiredRCMs == (e2 - r3).count());
  CHECK(
      stats3.value().mostExpiredRCMs < 0);  // ensure negative indicates unhealthy and we use min()
  CHECK(stats3.value().numDistinctRCsReceived == 3);
  CHECK(stats3.value().lastRCUpdated == s3.count());
}
