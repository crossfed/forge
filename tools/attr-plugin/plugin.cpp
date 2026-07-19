import forge.contract.attributes.registry;

namespace {

const auto registered = [] {
   forge::contract::attributes::register_all();
   return true;
}();

} // namespace
