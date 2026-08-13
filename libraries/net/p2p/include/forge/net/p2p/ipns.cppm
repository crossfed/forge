module;

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module forge.net.p2p.ipns;

import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

export namespace forge::net::p2p::ipns {

inline constexpr std::size_t max_record_size = 10 * 1024;
inline constexpr std::string_view routing_prefix = "/ipns/";

enum class validity_type : std::uint8_t {
   eol = 0,
};

class time_point {
 public:
   time_point() = default;
   time_point(std::chrono::sys_seconds whole_seconds, std::chrono::nanoseconds subsecond = {});
   time_point(std::chrono::sys_time<std::chrono::nanoseconds> value);

   [[nodiscard]] static time_point now();
   [[nodiscard]] std::chrono::sys_seconds whole_seconds() const noexcept;
   [[nodiscard]] std::chrono::nanoseconds subsecond() const noexcept;

   [[nodiscard]] friend bool operator==(const time_point&, const time_point&) noexcept = default;
   friend std::strong_ordering operator<=>(const time_point& left, const time_point& right) noexcept;

 private:
   std::chrono::sys_seconds whole_seconds_{};
   std::chrono::nanoseconds subsecond_{};
};
using metadata_value = std::variant<std::string, std::vector<std::uint8_t>, std::int64_t, bool>;
using metadata = std::map<std::string, metadata_value, std::less<>>;
// The callback must synchronously sign the supplied bytes without changing
// application state; identity/private-key ownership stays with the caller.
using signing_callback = std::function<std::vector<std::uint8_t>(std::span<const std::uint8_t> message)>;

struct create_options {
   bool v1_compatibility = true;
   std::optional<bool> embed_public_key;
   metadata metadata_values;
};

class record {
 public:
   record(const record&) = default;
   record(record&&) noexcept = default;
   record& operator=(const record&) = default;
   record& operator=(record&&) noexcept = default;
   ~record() = default;

   [[nodiscard]] std::span<const std::uint8_t> value() const noexcept;
   [[nodiscard]] std::uint64_t sequence() const noexcept;
   [[nodiscard]] time_point eol() const noexcept;
   [[nodiscard]] std::string_view eol_text() const noexcept;
   [[nodiscard]] std::chrono::nanoseconds ttl() const noexcept;
   [[nodiscard]] validity_type validity() const noexcept;
   [[nodiscard]] const metadata& metadata_values() const noexcept;
   [[nodiscard]] std::optional<public_key> embedded_public_key() const;
   [[nodiscard]] bool has_v1_compatibility() const noexcept;
   [[nodiscard]] bool has_v2_signature() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> signature_v1() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> signature_v2() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> data() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> encoded() const noexcept;

 private:
   record() = default;

   friend record create(const public_key&, const signing_callback&, std::span<const std::uint8_t>, std::uint64_t,
                        time_point, std::chrono::nanoseconds, create_options);
   friend record decode(std::span<const std::uint8_t>);
   friend std::vector<std::uint8_t> encode(const record&);
   friend void validate(const record&, const peer_id&, std::optional<public_key>, time_point);
   friend void validate(const record&, const peer_id&, const public_key_resolver&, time_point);
   friend std::size_t select(std::span<const record>);

   std::vector<std::uint8_t> encoded_;
   std::optional<std::vector<std::uint8_t>> value_v1_;
   std::optional<std::vector<std::uint8_t>> signature_v1_;
   std::optional<std::uint64_t> validity_type_v1_;
   std::optional<std::vector<std::uint8_t>> validity_v1_;
   std::optional<std::uint64_t> sequence_v1_;
   std::optional<std::uint64_t> ttl_v1_;
   std::optional<std::vector<std::uint8_t>> public_key_;
   std::optional<std::vector<std::uint8_t>> signature_v2_;
   std::optional<std::vector<std::uint8_t>> data_;
   std::vector<std::uint8_t> unknown_protobuf_fields_;

   std::vector<std::uint8_t> value_;
   std::uint64_t sequence_ = 0;
   time_point eol_{};
   std::string eol_text_;
   std::chrono::nanoseconds ttl_{};
   validity_type validity_ = validity_type::eol;
   metadata metadata_;
};

[[nodiscard]] record create(const public_key& key, const signing_callback& signer, std::span<const std::uint8_t> value,
                            std::uint64_t sequence, time_point eol, std::chrono::nanoseconds ttl,
                            create_options options = {});
[[nodiscard]] record decode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode(const record& value);

void validate(const record& value, const peer_id& expected_peer, std::optional<public_key> external_key = std::nullopt,
              time_point now = time_point::now());
void validate(const record& value, const peer_id& expected_peer, const public_key_resolver& resolver,
              time_point now = time_point::now());

[[nodiscard]] std::size_t select(std::span<const record> candidates);
[[nodiscard]] std::vector<std::uint8_t> routing_key(const peer_id& peer);

} // namespace forge::net::p2p::ipns
