module;

#include <memory>
#include <typeindex>
#include <unordered_map>

export module forge.api.core.trusted_invocation:trusted_invocation_state;

export namespace forge::api::core::detail {

class trusted_invocation_state final {
 public:
   void insert(std::type_index type, std::shared_ptr<const void> value);
   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] const void* find_exact(std::type_index type) const noexcept;

 private:
   std::unordered_map<std::type_index, std::shared_ptr<const void>> values_;
};

} // namespace forge::api::core::detail
