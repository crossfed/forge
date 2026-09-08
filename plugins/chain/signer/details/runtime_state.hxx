#pragma once

#include "admission.hxx"
#include "signing_policy.hxx"

#include <chrono>
#include <memory>

namespace forge::plugins::chain::signer {

struct runtime_state {
   std::shared_ptr<const signing_policy> policy;
   std::shared_ptr<admission_queue> admission;
   std::chrono::milliseconds shutdown_timeout;
};

} // namespace forge::plugins::chain::signer
