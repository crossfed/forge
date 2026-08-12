module;

#include <boost/describe.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.net.p2p.lifecycle;

import forge.net.p2p.endpoint;
import forge.net.p2p.identity;

export namespace forge::net::p2p {

enum class bootstrap_requirement : std::uint8_t {
   allow_disconnected = 1,
   require_connection = 2,
};

enum class lifecycle_phase : std::uint8_t {
   idle = 1,
   hydrating = 2,
   listening = 3,
   bootstrapping = 4,
   maintenance = 5,
   stopping = 6,
   stopped = 7,
};

BOOST_DESCRIBE_ENUM(bootstrap_requirement, allow_disconnected, require_connection)
BOOST_DESCRIBE_ENUM(lifecycle_phase, idle, hydrating, listening, bootstrapping, maintenance, stopping, stopped)

struct bootstrap_peer {
   forge::net::p2p::endpoint address;
};

struct lifecycle_status {
   lifecycle_phase phase = lifecycle_phase::idle;
   bootstrap_requirement requirement = bootstrap_requirement::allow_disconnected;
   std::size_t configured_bootstrap = 0;
   std::size_t connected_bootstrap = 0;
   bool degraded = false;
   std::string last_bootstrap_failure;
};

struct lifecycle_options {
   std::vector<forge::net::p2p::endpoint> listen;
   std::vector<bootstrap_peer> bootstrap;
   bootstrap_requirement requirement = bootstrap_requirement::allow_disconnected;
   std::chrono::milliseconds startup_budget{10'000};
   std::size_t max_parallel_bootstrap = 4;
   std::chrono::milliseconds connect_timeout{2'000};
   std::chrono::milliseconds bootstrap_retry_initial_delay{1'000};
   std::chrono::milliseconds bootstrap_retry_max_delay{30'000};
   double bootstrap_retry_jitter = 0.20;
   std::chrono::milliseconds maintenance_interval{1'000};
};

} // namespace forge::net::p2p

BOOST_DESCRIBE_STRUCT(forge::net::p2p::lifecycle_status, (),
                      (phase, requirement, configured_bootstrap, connected_bootstrap, degraded, last_bootstrap_failure))
