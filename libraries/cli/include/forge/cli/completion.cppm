module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.cli.completion;

import forge.cli.command;

export namespace forge::cli {

enum class completion_item_kind : std::uint8_t {
   command,
   option,
   value,
};

struct completion_item {
   std::string text;
   std::string description;
   completion_item_kind kind = completion_item_kind::value;
};

[[nodiscard]] std::vector<completion_item> complete(const application_descriptor& descriptor,
                                                    std::span<const std::string_view> words);

} // namespace forge::cli
