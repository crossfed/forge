#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

import forge.net.tls.context;
import forge.net.tls.exceptions;

int main() {
   auto options = forge::net::tls::context_options{};
   options.verification = forge::net::tls::peer_verification::none;
   options.use_default_verify_paths = false;

   auto provider = forge::net::tls::context_provider{options};
   const auto first = provider.snapshot();
   auto context = boost::asio::io_context{};
   auto generic_stream = forge::net::tls::make_asio_stream<boost::asio::ip::tcp::socket>(
       first, boost::asio::ip::tcp::socket{context});
   provider.replace(std::move(options));
   const auto second = provider.snapshot();
   const auto direct_exception_import = forge::net::tls::exceptions::code::fingerprint_mismatch;
   return first && second && generic_stream && first.get() != second.get() &&
                  direct_exception_import == forge::net::tls::exceptions::code::fingerprint_mismatch
              ? 0
              : 1;
}
