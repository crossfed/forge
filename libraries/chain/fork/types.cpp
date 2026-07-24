module forge.chain.fork.types;

namespace forge::chain::fork {

bool inserted(insert_status value) noexcept {
   return value == insert_status::inserted;
}

} // namespace forge::chain::fork
