module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.wasm_parameters;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct wasm_parameters {
   std::uint32_t max_mutable_global_bytes = 1'024U;
   std::uint32_t max_table_elements = 1'024U;
   std::uint32_t max_section_elements = 8'192U;
   std::uint32_t max_linear_memory_init = 64U * 1'024U;
   std::uint32_t max_func_local_bytes = 8'192U;
   std::uint32_t max_nested_structures = 1'024U;
   std::uint32_t max_symbol_bytes = 8'192U;
   std::uint32_t max_module_bytes = 20U * 1'024U * 1'024U;
   std::uint32_t max_code_bytes = 20U * 1'024U * 1'024U;
   std::uint32_t max_pages = 528U;
   std::uint32_t max_call_depth = 251U;

   bool operator==(const wasm_parameters&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const wasm_parameters& value) {
   forge::raw::pack(stream, value.max_mutable_global_bytes);
   forge::raw::pack(stream, value.max_table_elements);
   forge::raw::pack(stream, value.max_section_elements);
   forge::raw::pack(stream, value.max_linear_memory_init);
   forge::raw::pack(stream, value.max_func_local_bytes);
   forge::raw::pack(stream, value.max_nested_structures);
   forge::raw::pack(stream, value.max_symbol_bytes);
   forge::raw::pack(stream, value.max_module_bytes);
   forge::raw::pack(stream, value.max_code_bytes);
   forge::raw::pack(stream, value.max_pages);
   forge::raw::pack(stream, value.max_call_depth);
}

template <typename Stream> void raw_unpack(Stream& stream, wasm_parameters& value) {
   forge::raw::unpack(stream, value.max_mutable_global_bytes);
   forge::raw::unpack(stream, value.max_table_elements);
   forge::raw::unpack(stream, value.max_section_elements);
   forge::raw::unpack(stream, value.max_linear_memory_init);
   forge::raw::unpack(stream, value.max_func_local_bytes);
   forge::raw::unpack(stream, value.max_nested_structures);
   forge::raw::unpack(stream, value.max_symbol_bytes);
   forge::raw::unpack(stream, value.max_module_bytes);
   forge::raw::unpack(stream, value.max_code_bytes);
   forge::raw::unpack(stream, value.max_pages);
   forge::raw::unpack(stream, value.max_call_depth);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(wasm_parameters, (),
                      (max_mutable_global_bytes, max_table_elements, max_section_elements, max_linear_memory_init,
                       max_func_local_bytes, max_nested_structures, max_symbol_bytes, max_module_bytes, max_code_bytes,
                       max_pages, max_call_depth))
} // namespace forge::chain::protocol
#endif
