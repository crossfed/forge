#include "multi_source.hpp"

multi_source_types::value increment(multi_source_types::value value) {
   auto packed = forge::raw::pack(value);
   auto result = forge::raw::unpack_exact<multi_source_types::value>(packed);
   ++result.number;
   return result;
}
