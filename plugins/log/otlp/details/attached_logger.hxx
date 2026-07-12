#pragma once

namespace forge::plugins::log::otlp {

struct attached_logger {
   std::string name;
   forge::logger logger;
};

} // namespace forge::plugins::log::otlp
