module;

#include <functional>
#include <memory>

export module forge.api.p2p.publication;

export namespace forge::api::p2p {

namespace detail {
class publication_impl;
struct publication_access;
} // namespace detail

class publication {
 public:
   publication() = default;
   ~publication();

   publication(const publication&) = delete;
   publication& operator=(const publication&) = delete;
   publication(publication&& other) noexcept;
   publication& operator=(publication&& other) noexcept;

   [[nodiscard]] bool active() const noexcept;
   void close() noexcept;

 private:
   explicit publication(std::function<void()> close);

   friend struct detail::publication_access;

   std::shared_ptr<detail::publication_impl> impl_;
};

namespace detail {

struct publication_access {
   [[nodiscard]] static publication make(std::function<void()> close);
   [[nodiscard]] static std::function<void()> close_callback(const publication& value);
};

} // namespace detail

} // namespace forge::api::p2p
