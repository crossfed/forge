module;

#include <new>

module forge.net.dns.resolver;

#include "details/resolver_wait_failure.hxx"

namespace forge::net::dns {
namespace {

thread_local resolver_wait_failure* active_failure = nullptr;

} // namespace

resolver_wait_failure::resolver_wait_failure(point where) noexcept
    : _where(where), _previous(active_failure) {
   active_failure = this;
}

resolver_wait_failure::~resolver_wait_failure() {
   active_failure = _previous;
}

bool resolver_wait_failure::triggered() const noexcept {
   return _triggered;
}

void resolver_wait_failure::check(point where) {
   if (active_failure && active_failure->_where == where && !active_failure->_triggered) {
      active_failure->_triggered = true;
      throw std::bad_alloc{};
   }
}

} // namespace forge::net::dns
