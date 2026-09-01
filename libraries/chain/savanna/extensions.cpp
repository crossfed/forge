module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

module forge.chain.savanna.extensions;

import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

template <typename Value> Value decode(const forge::chain::protocol::bytes& bytes, const char* description) {
   try {
      if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension, description);
      }
      const auto payload_budget = static_cast<std::uint32_t>(bytes.size());
      const auto limits = forge::raw::unpack_limits{
          .max_container_elements = payload_budget,
          .max_total_container_elements = payload_budget,
          .max_bytes = payload_budget,
          .first_container_elements = payload_budget,
      };
      auto value = forge::raw::unpack_exact<Value>(bytes, limits);
      const auto canonical = forge::raw::pack(value);
      if (canonical.size() != bytes.size() || !std::ranges::equal(canonical, bytes)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension, description);
      }
      return value;
   } catch (const exceptions::invalid_extension&) {
      throw;
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_extension, description);
   }
}

template <typename Handler>
void visit_ordered(const forge::chain::protocol::extensions& extensions, Handler&& handler) {
   auto previous = std::optional<std::uint16_t>{};
   for (const auto& [id, bytes] : extensions) {
      if (previous && id <= *previous) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "Savanna extensions must be ordered and unique");
      }
      handler(id, bytes);
      previous = id;
   }
}

} // namespace

header_extensions decode_header_extensions(const forge::chain::protocol::extensions& extensions) {
   auto result = header_extensions{};
   auto has_finality = false;
   auto has_state_commitment = false;
   visit_ordered(extensions, [&](std::uint16_t id, const auto& bytes) {
      switch (id) {
      case protocol_feature_extension_id:
         result.protocol_features =
             decode<protocol_feature_extension>(bytes, "Savanna protocol feature extension is invalid").features;
         break;
      case finality_extension_id:
         result.finality = decode<finality_extension>(bytes, "Savanna finality extension is invalid");
         has_finality = true;
         break;
      case finalizer_proof_extension_id:
         result.finalizer_proofs =
             decode<finalizer_proof_extension>(bytes, "Savanna finalizer proof extension is invalid");
         break;
      case state_commitment_extension_id:
         result.commitment = decode<state_commitment>(bytes, "Savanna state commitment extension is invalid");
         if (result.commitment.version != state_commitment_version) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_extension,
                                  "Savanna state commitment extension version is unsupported");
         }
         has_state_commitment = true;
         break;
      default:
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "unsupported Savanna header extension");
      }
   });
   if (!has_finality) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "Savanna finality extension is missing");
   }
   if (!has_state_commitment) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "Savanna state commitment extension is missing");
   }
   return result;
}

block_extensions decode_block_extensions(const forge::chain::protocol::extensions& extensions) {
   auto result = block_extensions{};
   visit_ordered(extensions, [&](std::uint16_t id, const auto& bytes) {
      switch (id) {
      case additional_signatures_extension_id: {
         result.additional_signatures =
             decode<additional_signatures_extension>(bytes, "Savanna additional signatures extension is invalid")
                 .signatures;
         if (result.additional_signatures.empty()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "Savanna additional signatures extension is empty");
         }
         break;
      }
      case quorum_certificate_extension_id:
         result.certificate =
             decode<quorum_certificate_extension>(bytes, "Savanna quorum certificate extension is invalid").certificate;
         break;
      default:
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension, "unsupported Savanna block extension");
      }
   });
   return result;
}

} // namespace forge::chain::savanna
